import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { VLVTable } from "./databaseTables.jsx";
import { log_cmd } from "../tools.jsx";
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
	Text,
	TextContent,
	TextVariants,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectVariant,
	SelectOption
} from '@patternfly/react-core/deprecated';
import PropTypes from "prop-types";

const _ = cockpit.gettext;

export class VLVIndexes extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            vlvItems: [],
            showVLVModal: false,
            showDeleteConfirm: false,
            showReindexConfirm: false,
            isVlvSortIndexSelectOpen: false,
            saving: false,
            saveBtnDisabled: true,
            updating: false,
            modalSpinning: false,
            modalChecked: false,
            showCreateSortIndex: false,
            showDeleteSortIndexConfirm: false,
            createIndexParent: "",
            deleteIndexParent: "",
            deleteIndexName: "",
            deleteVLVName: "",
            errObj: {},
            vlvName: "",
            vlvBase: "",
            vlvScope: "subtree",
            vlvFilter: "",
            vlvSortList: [],
            _vlvName: "",
            _vlvBase: "",
            _vlvScope: "",
            _vlvFilter: "",
            _vlvSortList: [],
        };

        // Create VLV Modal
        this.handleShowVLVModal = this.handleShowVLVModal.bind(this);
        this.closeVLVModal = this.closeVLVModal.bind(this);
        this.onModalChange = this.onModalChange.bind(this);
        this.onVLVChange = this.onVLVChange.bind(this);
        this.saveVLV = this.saveVLV.bind(this);
        this.deleteVLV = this.deleteVLV.bind(this);
        this.reindexVLV = this.reindexVLV.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.showReindexConfirm = this.showReindexConfirm.bind(this);
        this.closeReindexConfirm = this.closeReindexConfirm.bind(this);
        // Select typeahead
        this.handleSelectToggle = this.handleSelectToggle.bind(this);
        this.handleSelectClear = this.handleSelectClear.bind(this);
        this.deleteSortIndex = this.deleteSortIndex.bind(this);
        this.createSortIndex = this.createSortIndex.bind(this);
        // Sort index
        this.showCreateSortIndex = this.showCreateSortIndex.bind(this);
        this.closeCreateSortIndex = this.closeCreateSortIndex.bind(this);
        this.showDeleteSortIndexConfirm = this.showDeleteSortIndexConfirm.bind(this);
        this.closeDeleteSortIndexConfirm = this.closeDeleteSortIndexConfirm.bind(this);
    }

    //
    // VLV index functions
    //
    handleShowVLVModal() {
        this.setState({
            showVLVModal: true,
            errObj: {},
            vlvName: "",
            vlvBase: "",
            vlvScope: "subtree",
            vlvFilter: "",
            vlvSortList: [],
            saving: false,
        });
    }

    closeVLVModal() {
        this.setState({
            showVLVModal: false
        });
    }

    onVLVChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        const attr = e.target.id;
        let saveBtnDisabled = false;
        const vlvCreateAttrs = ['vlvName', 'vlvBase', 'vlvFilter'];

        for (const createAttr of vlvCreateAttrs) {
            if (attr !== createAttr && this.state[createAttr] === "") {
                saveBtnDisabled = true;
            }
        }
        if (value === "") {
            valueErr = true;
            saveBtnDisabled = true;
        }

        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj,
            saveBtnDisabled
        });
    }

    onModalChange(e) {
        // Basic handler, no error checking
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    getScopeVal(scope) {
        const mapping = {
            subtree: '2',
            one: '1',
            base: '0'
        };
        return mapping[scope];
    }

    showCreateSortIndex(parent) {
        this.setState({
            showCreateSortIndex: true,
            createIndexParent: parent,
        });
    }

    closeCreateSortIndex() {
        this.setState({
            showCreateSortIndex: false,
            createIndexParent: "",
        });
    }

    createSortIndex(index) {
        const index_value = index.join(' ');
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "add-index", "--parent-name=" + this.state.createIndexParent,
            "--index-name=" + this.state.createIndexParent + " - " + index_value,
            "--sort=" + index_value, this.props.suffix
        ];
        if (this.state.reindexVLV) {
            cmd.push("--index-it");
        }

        this.setState({
            updating: true,
        });

        log_cmd("createSortIndex", "Add index", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeCreateSortIndex();
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        _("Successfully added VLV sort index")
                    );
                    this.setState({
                        updating: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeCreateSortIndex();
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to add VLV index entry - $0"), errMsg.desc)
                    );
                    this.setState({
                        updating: false,
                    });
                });
    }

    showDeleteSortIndexConfirm(parent, name) {
        this.setState({
            showDeleteSortIndexConfirm: true,
            deleteIndexParent: parent,
            deleteIndexName: name
        });
    }

    closeDeleteSortIndexConfirm() {
        this.setState({
            showDeleteSortIndexConfirm: false,
            deleteIndexParent: "",
            deleteIndexName: "",
        });
    }

    deleteSortIndex() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "del-index", "--parent-name=" + this.state.deleteIndexParent,
            "--sort=" + this.state.deleteIndexName, this.props.suffix
        ];
        this.setState({
            updating: true,
            modalSpinning: true,
        });
        log_cmd("deleteSortIndex", "delete index", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeDeleteSortIndexConfirm();
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        _("Successfully removed VLV sort index")
                    );
                    this.setState({
                        updating: false,
                        modalSpinning: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeDeleteSortIndexConfirm();
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to delete VLV sort index - $0"), errMsg.desc)
                    );
                    this.setState({
                        updating: false,
                        modalSpinning: false,
                    });
                });
    }

    saveVLV() {
        this.setState({
            saving: true
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "add-search",
            "--name=" + this.state.vlvName,
            "--search-base=" + this.state.vlvBase,
            "--search-filter=" + this.state.vlvFilter,
            "--search-scope=" + this.getScopeVal(this.state.vlvScope),
            this.props.suffix
        ];
        log_cmd("saveVLV", "Add vlv search", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeVLVModal();
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        _("Successfully added VLV search: ") + this.state.vlvName
                    );
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeVLVModal();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed create VLV search entry - $0"), errMsg.desc)
                    );
                    this.setState({
                        saving: false
                    });
                });
    }

    showDeleteConfirm(name) {
        this.setState({
            showDeleteConfirm: true,
            deleteVLVName: name
        });
    }

    closeDeleteConfirm () {
        this.setState({
            showDeleteConfirm: false,
        });
    }

    deleteVLV() {
        this.setState({
            modalSpinning: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "del-search", "--name=" + this.state.deleteVLVName, this.props.suffix
        ];
        log_cmd("deleteVLV", "delete LV search and indexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        _("Successfully deleted VLV index")
                    );
                    this.setState({
                        modalSpinning: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to deletre VLV index - $0"), errMsg.desc)
                    );
                    this.setState({
                        modalSpinning: false
                    });
                });
    }

    showReindexConfirm (name) {
        this.setState({
            showReindexConfirm: true,
            reindexVLVName: name
        });
    }

    closeReindexConfirm () {
        this.setState({
            showReindexConfirm: false,
        });
    }

    reindexVLV() {
        this.setState({
            modalSpinning: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "reindex", "--parent-name=" + this.state.reindexVLVName, this.props.suffix
        ];
        log_cmd("reindexVLV", "reindex VLV indexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        _("Successfully completed VLV indexing")
                    );
                    this.setState({
                        modalSpinning: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to index VLV index - $0"), errMsg.desc)
                    );
                    this.setState({
                        modalSpinning: false
                    });
                });
    }

    handleSelectToggle = (_event, isExpanded, toggleId) => {
        this.setState({
            [toggleId]: isExpanded
        });
    };

    handleSelectClear = item => event => {
        this.setState({
            sortValue: [],
            isVlvSortIndexSelectOpen: false
        });
    };

    render() {
        return (
            <div className="ds-tab-table ds-margin-bottom-md">
                <VLVTable
                    rows={this.props.vlvItems}
                    key={this.props.vlvItems}
                    deleteFunc={this.showDeleteConfirm}
                    reindexFunc={this.showReindexConfirm}
                    deleteSortFunc={this.showDeleteSortIndexConfirm}
                    addSortFunc={this.showCreateSortIndex}
                    updating={this.state.updating}
                />
                <Button
                    variant="primary"
                    onClick={this.handleShowVLVModal}
                >
                    {_("Create VLV Index")}
                </Button>
                <AddVLVIndexModal
                    showModal={this.state.showCreateSortIndex}
                    handleClose={this.closeCreateSortIndex}
                    handleChange={this.onModalChange}
                    saveHandler={this.createSortIndex}
                    onSelectToggle={this.handleSelectToggle}
                    onSelectClear={this.handleSelectClear}
                    handleTypeaheadChange={this.onTypeaheadChange}
                    attrs={this.props.attrs}
                    reindexVLV={this.state.reindexVLV}
                    vlvSortList={this.state.vlvSortList}
                    saving={this.state.saving || this.state.updating}
                    saveBtnDisabled={this.state.saveBtnDisabled}
                />
                <AddVLVModal
                    showModal={this.state.showVLVModal}
                    closeHandler={this.closeVLVModal}
                    handleChange={this.onVLVChange}
                    saveHandler={this.saveVLV}
                    error={this.state.errObj}
                    vlvName={this.state.vlvName}
                    vlvBase={this.state.vlvBase}
                    vlvScope={this.state.vlvScope}
                    vlvFilter={this.state.vlvFilter}
                    saving={this.state.saving}
                    saveBtnDisabled={this.state.saveBtnDisabled}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.onModalChange}
                    actionHandler={this.deleteVLV}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteVLVName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete VLV Index")}
                    mMsg={_("Are you sure you want to delete this VLV index??")}
                    mSpinningMsg={_("Deleting Index ...")}
                    mBtnName={_("Delete Index")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showReindexConfirm}
                    closeHandler={this.closeReindexConfirm}
                    handleChange={this.onModalChange}
                    actionHandler={this.reindexVLV}
                    spinning={this.state.modalSpinning}
                    item={this.state.reindexVLVName}
                    checked={this.state.modalChecked}
                    mTitle={_("Reindex VLV Index")}
                    mMsg={_("Are you sure you want to reindex this VLV index??")}
                    mSpinningMsg={_("Reindex ...")}
                    mBtnName={_("Reindex")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDeleteSortIndexConfirm}
                    closeHandler={this.closeDeleteSortIndexConfirm}
                    handleChange={this.onModalChange}
                    actionHandler={this.deleteSortIndex}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteIndexName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete VLV Sort Index")}
                    mMsg={_("Are you really sure you want to delete this sorting index?")}
                    mSpinningMsg={_("Deleting Index ...")}
                    mBtnName={_("Delete Index")}
                />
            </div>
        );
    }
}

// Add Sort Index modal
class AddVLVIndexModal extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortValue: [],
            isVLVSortOpen: false,
        };

        // VLV Sort indexes
        this.handleVLVSortToggle = (_event, isVLVSortOpen) => {
            this.setState({
                isVLVSortOpen
            });
        };
        this.handleVLVSortClear = () => {
            this.setState({
                sortValue: [],
                isVLVSortOpen: false
            });
        };

        this.onTypeaheadChange = this.onTypeaheadChange.bind(this);
    }

    onTypeaheadChange(e, selection) {
        if (this.state.sortValue.includes(selection)) {
            this.setState(
                (prevState) => ({
                    sortValue: prevState.sortValue.filter((item) => item !== selection),
                    isVLVSortOpen: false
                }),
            );
        } else {
            this.setState(
                (prevState) => ({
                    sortValue: [...prevState.sortValue, selection],
                    isVLVSortOpen: false
                }),
            );
        }
    }

    render() {
        const {
            showModal,
            handleChange,
            attrs,
            saving,
        } = this.props;
        let saveBtnName = _("Create Sort Index");
        const extraPrimaryProps = {};
        if (this.props.saving) {
            saveBtnName = _("Creating Index ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={_("Create VLV Sort Index")}
                isOpen={showModal}
                onClose={this.props.handleClose}
                className={this.state.isVLVSortOpen ? "ds-modal-select-tall" : "ds-modal-select"}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={() => {
                            this.props.handleSave(this.state.sortValue);
                        }}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.state.sortValue.length === 0 || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={this.props.handleClose}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={12}>
                            {_("Build a list of attributes to form the \"Sort\" index")}
                        </GridItem>
                        <Select
                            variant={SelectVariant.typeaheadMulti}
                            typeAheadAriaLabel={_("Type an attribute names to create a sort index")}
                            className="ds-margin-top-lg"
                            onToggle={(event, isOpen) => this.handleVLVSortToggle(event, isOpen)}
                            onClear={this.handleVLVSortClear}
                            onSelect={this.handleTypeaheadChange}
                            maxHeight={1000}
                            selections={this.state.sortValue}
                            isOpen={this.state.isVLVSortOpen}
                            aria-labelledby="typeAhead-vlv-sort-index"
                            placeholderText={_("Type an attribute name ...")}
                            noResultsFoundText={_("There are no matching entries")}
                        >
                            {attrs.map((attr, index) => (
                                <SelectOption
                                    key={index}
                                    value={attr}
                                />
                            ))}
                        </Select>
                        <GridItem className="ds-margin-top-xlg" span={12}>
                            <Checkbox
                                id="reindexVLV"
                                isChecked={this.props.reindexVLV}
                                onChange={(e, checked) => {
                                    handleChange(e);
                                }}
                                label={_("Reindex After Saving")}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

// Add modal
class AddVLVModal extends React.Component {
    render() {
        const {
            showModal,
            handleChange,
            saveHandler,
            closeHandler,
            error,
            saving
        } = this.props;
        let saveBtnName = _("Save VLV Index");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Saving Index ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={_("Create VLV Search Index")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={2}>
                            {_("VLV Index Name")}
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                value={this.props.vlvName}
                                type="text"
                                id="vlvName"
                                aria-describedby="vlvName"
                                name="vlvName"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={error.vlvName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={2}>
                            {_("Search Base")}
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                value={this.props.vlvBase}
                                type="text"
                                id="vlvBase"
                                aria-describedby="vlvBase"
                                name="vlvBase"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={error.vlvBase ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={2}>
                            {_("Search Filter")}
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                type="text"
                                id="vlvFilter"
                                aria-describedby="vlvFilter"
                                name="vlvFilter"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                value={this.props.filter}
                                validated={error.vlvFilter ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={2}>
                            {_("Search Scope")}
                        </GridItem>
                        <GridItem span={10}>
                            <FormSelect
                                value={this.props.vlvScope}
                                onChange={(event, value) => {
                                    handleChange(event);
                                }}
                                id="vlvScope"
                                aria-label="FormSelect Input"
                            >
                                <FormSelectOption key="0" value="subtree" label="subtree" />
                                <FormSelectOption key="1" value="one" label="one" />
                                <FormSelectOption key="2" value="base" label="base" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem offset={1} className="ds-margin-top-lg ds-margin-bottom" span={10}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    {_("After creating this VLV Search entry you can go to the table and add VLV Sort Indexes to this VLV Search.  After adding the Sort Indexes you will need to <i>reindex</i> the VLV Index to make it active.")}
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

// Property types and defaults

VLVIndexes.propTypes = {
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    vlvItems: PropTypes.array,
    addNotification: PropTypes.func,
    attrs: PropTypes.array,
    reload: PropTypes.func,
};

VLVIndexes.defaultProps = {
    suffix: "",
    serverId: "",
    vlvItems: [],
    attrs: [],
};

AddVLVModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    handleSortChange: PropTypes.func,
    saveHandler: PropTypes.func,
    error: PropTypes.object,
    attrs: PropTypes.array,
    vlvName: PropTypes.string,
    vlvBase: PropTypes.string,
    vlvScope: PropTypes.string,
    vlvFilter: PropTypes.string,
    vlvSortList: PropTypes.array,
};

AddVLVModal.defaultProps = {
    showModal: false,
    error: {},
    attrs: [],
    vlvName: "",
    vlvBase: "",
    vlvScope: "",
    vlvFilter: "",
    vlvSortList: [],
};
