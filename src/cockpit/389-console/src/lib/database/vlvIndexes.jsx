import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { log_cmd } from "../tools.jsx";
import {
    DropdownButton,
    MenuItem,
    ListView,
    ListViewItem,
    ListViewIcon,
    Modal,
    Row,
    Checkbox,
    Col,
    ControlLabel,
    Icon,
    Button,
    Form,
    sortableHeaderCellFormatter,
    actionHeaderCellFormatter,
    tableCellFormatter,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";
import { Typeahead } from "react-bootstrap-typeahead";
import { DSShortTable } from "../dsTable.jsx";
import "../../css/ds.css";

export class VLVIndexes extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            vlvItems: [],
            showVLVModal: false,
            showVLVEditModal: false,
            showDeleteConfirm: false,
            showReindexConfirm: false,
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
        this.showVLVModal = this.showVLVModal.bind(this);
        this.closeVLVModal = this.closeVLVModal.bind(this);
        this.handleVLVChange = this.handleVLVChange.bind(this);
        this.handleVLVSortChange = this.handleVLVSortChange.bind(this);
        this.saveVLV = this.saveVLV.bind(this);
        this.saveEditVLV = this.saveEditVLV.bind(this);
        this.deleteVLV = this.deleteVLV.bind(this);
        this.reindexVLV = this.reindexVLV.bind(this);
        // Edit VLV modal
        this.showVLVEditModal = this.showVLVEditModal.bind(this);
        this.closeVLVEditModal = this.closeVLVEditModal.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.showReindexConfirm = this.showReindexConfirm.bind(this);
        this.closeReindexConfirm = this.closeReindexConfirm.bind(this);
    }

    //
    // VLV index functions
    //
    showVLVModal() {
        this.setState({
            showVLVModal: true,
            errObj: {},
            vlvName: "",
            vlvBase: "",
            vlvScope: "subtree",
            vlvFilter: "",
            vlvSortList: [],
            vlvSortRows: [],
        });
    }

    closeVLVModal() {
        this.setState({
            showVLVModal: false
        });
    }

    handleVLVChange(e) {
        const value = e.target.value;
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

    handleVLVSortChange(sorts) {
        // "sorts" is a table obj that uses the key "sortName"
        let sortList = [];
        for (let sort of sorts) {
            // Create array of sorts from array of objs
            sortList.push(sort.sortName);
        }
        this.setState({
            vlvSortList: sortList
        });
    }

    // Edit VLVIndex
    showVLVEditModal(e) {
        const vlvName = e.target.name;
        for (let item of this.props.vlvItems) {
            const vlvAttrs = item.attrs;
            if (vlvAttrs.cn[0] == vlvName) {
                let sortRows = [];
                let sortList = [];
                for (let vlvSort of item.sorts) {
                    sortRows.push({sortName: vlvSort.attrs.vlvsort[0]});
                    sortList.push(vlvSort.attrs.vlvsort[0]);
                }
                this.setState({
                    showVLVEditModal: true,
                    errObj: {},
                    vlvName: vlvAttrs.cn[0],
                    vlvBase: vlvAttrs.vlvbase[0],
                    vlvScope: this.getScopeKey(vlvAttrs.vlvscope[0]),
                    vlvFilter: vlvAttrs.vlvfilter[0],
                    vlvSortList: sortList,
                    vlvSortRows: sortRows,
                    _vlvName: vlvAttrs.cn[0],
                    _vlvBase: vlvAttrs.vlvbase[0],
                    _vlvScope: this.getScopeKey(vlvAttrs.vlvscope[0]),
                    _vlvFilter: vlvAttrs.vlvfilter[0],
                    _vlvSortList: sortList,
                    _vlvSortRows: sortRows,
                });
                break;
            }
        }
    }

    closeVLVEditModal() {
        this.setState({
            showVLVEditModal: false
        });
    }

    getScopeVal(scope) {
        let mapping = {
            'subtree': '2',
            'one': '1',
            'base': '0'
        };
        return mapping[scope];
    }

    getScopeKey(scope) {
        let mapping = {
            '2': 'subtree',
            '1': 'one',
            '0': 'base'
        };
        return mapping[scope];
    }

    saveEditVLV() {
        let missingArgs = {};
        let errors = false;
        if (this.state.vlvBase == "") {
            this.props.addNotification(
                "warning",
                `Missing VLV search base`
            );
            missingArgs.vlvBase = true;
            errors = true;
        }
        if (this.state.vlvFilter == "") {
            this.props.addNotification(
                "warning",
                `Missing VLV search filter`
            );
            missingArgs.vlvFilter = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "edit-search", "--name=" + this.state.vlvName,
            this.props.suffix
        ];
        if (this.state.vlvBase != this.state._vlvBase) {
            cmd.push("--search-base=" + this.state.vlvBase);
        }
        if (this.state.vlvScope != this.state._vlvScope) {
            cmd.push("--search-scope=" + this.getScopeVal(this.state.vlvScope));
        }
        if (this.state.vlvFilter != this.state._vlvFilter) {
            cmd.push("--search-filter=" + this.state.vlvFilter);
        }
        if (cmd.length > 8) {
            log_cmd("saveEditVLV", "Add vlv search", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.closeVLVEditModal();
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "success",
                            "Successfully edited VLV search"
                        );
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.closeVLVEditModal();
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "error",
                            `Failed to edit VLV search - ${errMsg.desc}`
                        );
                    });
        }

        // Check the sort indexes now
        // Loop over sorts and create indexes or each one
        let addIndexList = [];
        let delIndexList = [];
        for (let sort of this.state.vlvSortList) {
            if (this.state._vlvSortList.indexOf(sort) == -1) {
                // Add sort index
                addIndexList.push(sort);
            }
        }
        for (let sort of this.state._vlvSortList) {
            if (this.state.vlvSortList.indexOf(sort) == -1) {
                // Del sort index
                delIndexList.push(sort);
            }
        }

        // Add VLV index/sort
        for (let index of addIndexList) {
            cmd = [
                "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "backend", "vlv-index", "add-index", "--parent-name=" + this.state.vlvName,
                "--index-name=" + this.state.vlvName + " - " + index,
                "--sort=" + index, this.props.suffix
            ];
            if (this.state.reindexVLV) {
                cmd.push("--index-it");
            }
            log_cmd("saveEditVLV", "Add index", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.closeVLVEditModal();
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "success",
                            "Successfully added VLV index " + this.state.vlvName + " - " + index
                        );
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.closeVLVEditModal();
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "error",
                            `Failed to add VLV index entry - ${errMsg.desc}`
                        );
                    });
        }

        // Del VLV index/sort
        for (let index of delIndexList) {
            cmd = [
                "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "backend", "vlv-index", "del-index", "--parent-name=" + this.state.vlvName,
                "--sort=" + index, this.props.suffix
            ];
            if (this.state.reindexVLV) {
                cmd.push("--index-it");
            }
            log_cmd("saveEditVLV", "delete index", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.closeVLVEditModal();
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "success",
                            "Successfully added VLV index " + this.state.vlvName + " - " + index
                        );
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.closeVLVEditModal();
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "error",
                            `Failed to add VLV index entry - ${errMsg.desc}`
                        );
                    });
        }
    }

    saveVLV() {
        let missingArgs = {};
        let errors = false;
        if (this.state.vlvName == "") {
            this.props.addNotification(
                "warning",
                `Missing VLV Search Name`
            );
            missingArgs.vlvName = true;
            errors = true;
        }
        if (this.state.vlvBase == "") {
            this.props.addNotification(
                "warning",
                `Missing VLV search base`
            );
            missingArgs.vlvBase = true;
            errors = true;
        }
        if (this.state.vlvFilter == "") {
            this.props.addNotification(
                "warning",
                `Missing VLV search filter`
            );
            missingArgs.vlvFilter = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        let cmd = [
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
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        "Successfully added VLV search: " + this.state.vlvName
                    );
                    // Loop over sorts and create indexes or each one
                    for (let sort of this.state.vlvSortList) {
                        const indexName = this.state.vlvName + " - " + sort;
                        let idx_cmd = [
                            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                            "backend", "vlv-index", "add-index",
                            "--parent-name=" + this.state.vlvName,
                            "--index-name=" + indexName,
                            "--sort=" + sort,
                            this.props.suffix
                        ];
                        if (this.state.reindexVLV) {
                            idx_cmd.push("--index");
                        }
                        log_cmd("saveVLV", "Add vlv index", idx_cmd);
                        cockpit
                                .spawn(idx_cmd, { superuser: true, err: "message" })
                                .done(content => {
                                    this.props.reload(this.props.suffix);
                                    this.props.addNotification(
                                        "success",
                                        "Successfully added VLV index: " + indexName
                                    );
                                })
                                .fail(err => {
                                    let errMsg = JSON.parse(err);
                                    this.props.addNotification(
                                        "error",
                                        `Failed create VLV index entry - ${errMsg.desc}`
                                    );
                                });
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed create VLV search entry - ${errMsg.desc}`
                    );
                });
        this.closeVLVModal();
    }

    renderVLVActions(id) {
        return (
            <div>
                <DropdownButton bsStyle="default" title="Actions" id={id}>
                    <MenuItem eventKey="1" name={id} onClick={this.showVLVEditModal}>
                        Edit VLV Index
                    </MenuItem>
                    <MenuItem eventKey="2" name={id} onClick={this.showReindexConfirm}>
                        Reindex VLV Index
                    </MenuItem>
                    <MenuItem divider />
                    <MenuItem eventKey="3" name={id} onClick={this.showDeleteConfirm}>
                        Delete VLV Index
                    </MenuItem>
                </DropdownButton>
            </div>
        );
    }

    showDeleteConfirm (e) {
        this.setState({
            showDeleteConfirm: true,
            deleteVLVName: e.target.name
        });
    }

    closeDeleteConfirm () {
        this.setState({
            showDeleteConfirm: false,
        });
    }

    deleteVLV(vlv) {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "del-search", "--name=" + vlv, this.props.suffix
        ];
        log_cmd("deleteVLV", "delete LV search and indexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Successfully deleted VLV index`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failed to deletre VLV index - ${errMsg.desc}`
                    );
                });
    }

    showReindexConfirm (e) {
        this.setState({
            showReindexConfirm: true,
            reindexVLVName: e.target.name
        });
    }

    closeReindexConfirm () {
        this.setState({
            showReindexConfirm: false,
        });
    }

    reindexVLV(vlv) {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "reindex", "--parent-name=" + vlv, this.props.suffix
        ];
        log_cmd("reindexVLV", "reindex VLV indexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Successfully completed VLV indexing`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failed to index VLV index - ${errMsg.desc}`
                    );
                });
    }

    render() {
        const vlvIndexes = this.props.vlvItems.map((vlvItem) =>
            <ListViewItem
                actions={this.renderVLVActions(vlvItem.attrs.cn[0])}
                leftContent={<ListViewIcon name="list" />}
                key={vlvItem.attrs.cn[0]}
                heading={vlvItem.attrs.cn[0]}
            >
                <Row>
                    <Col sm={11} key={vlvItem.dn}>
                        <p key={vlvItem.dn + "-p"}><label className="ds-vlv-label">Base</label>{vlvItem.attrs.vlvbase[0]}</p>
                        <p><label className="ds-vlv-label">Filter</label>{vlvItem.attrs.vlvfilter[0]}</p>
                        <p><label className="ds-vlv-label">Scope</label>{this.getScopeKey(vlvItem.attrs.vlvscope[0])}</p>
                        <hr />
                        {
                            vlvItem.sorts.map(sort => {
                                let indexState;
                                if (sort.attrs.vlvenabled[0] == "0") {
                                    indexState = <font size="1" color="#d01c8b"><b>Disabled</b></font>;
                                } else {
                                    indexState = <font size="1" color="#4dac26"><b>Uses: </b>{sort.attrs.vlvuses[0]}</font>;
                                }
                                return (<p key={sort.dn + sort.attrs.vlvsort[0]}><label className="ds-vlv-label">Sort</label>{sort.attrs.vlvsort[0]} ({indexState})</p>);
                            })
                        }
                    </Col>
                </Row>
            </ListViewItem>
        );

        return (
            <div className="ds-tab-table">
                <h5>Virtual List View Indexes</h5>
                <ListView>
                    {vlvIndexes}
                </ListView>
                <button className="btn btn-primary ds-margin-top" onClick={this.showVLVModal} type="button">Create VLV Index</button>
                <AddVLVModal
                    showModal={this.state.showVLVModal}
                    closeHandler={this.closeVLVModal}
                    handleChange={this.handleVLVChange}
                    handleSortChange={this.handleVLVSortChange}
                    saveHandler={this.saveVLV}
                    error={this.state.errObj}
                    attrs={this.props.attrs}
                />
                <AddVLVModal
                    showModal={this.state.showVLVEditModal}
                    closeHandler={this.closeVLVEditModal}
                    handleChange={this.handleVLVChange}
                    handleSortChange={this.handleVLVSortChange}
                    saveHandler={this.saveEditVLV}
                    edit
                    error={this.state.errObj}
                    attrs={this.props.attrs}
                    vlvName={this.state.vlvName}
                    vlvBase={this.state.vlvBase}
                    vlvScope={this.state.vlvScope}
                    vlvFilter={this.state.vlvFilter}
                    vlvSortList={this.state.vlvSortRows}
                />
                <ConfirmPopup
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    actionFunc={this.deleteVLV}
                    actionParam={this.state.deleteVLVName}
                    msg="Are you sure you want to delete this VLV index?"
                    msgContent={this.state.deleteVLVName}
                />
                <ConfirmPopup
                    showModal={this.state.showReindexConfirm}
                    closeHandler={this.closeReindexConfirm}
                    actionFunc={this.reindexVLV}
                    actionParam={this.state.reindexVLVName}
                    msg="Are you sure you want to reindex this VLV index?"
                    msgContent={this.state.reindexVLVName}
                />
            </div>
        );
    }
}

// Add and edit modal
class AddVLVModal extends React.Component {
    constructor(props) {
        super(props);
        let sortRows = [];
        if (this.props.edit !== undefined && this.props.vlvSortList !== undefined) {
            sortRows = this.props.vlvSortList;
        }
        this.state = {
            sortRows: sortRows,
            sortValue: "",
            columns: [],
            edit: false,
        };

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
        this.updateSorts = this.updateSorts.bind(this);
        this.handleVLVSortChange = this.handleVLVSortChange.bind(this);
        this.close = this.close.bind(this);
        this.save = this.save.bind(this);
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "VLV Sort Indexes",
                    props: {
                        index: 0,
                        rowSpan: 1,
                        colSpan: 1,
                        sort: true
                    },
                    transforms: [],
                    formatters: [],
                    customFormatters: [sortableHeaderCellFormatter]
                },
                cell: {
                    props: {
                        index: 0
                    },
                    formatters: [tableCellFormatter]
                }
            },
        ];
    }

    getColumns () {
        return [
            {
                property: "sortName",
                header: {
                    label: "VLV Sort Indexes",
                    props: {
                        index: 0,
                        rowSpan: 1,
                        colSpan: 1,
                        sort: true
                    },
                    transforms: [],
                    formatters: [],
                    customFormatters: [sortableHeaderCellFormatter]
                },
                cell: {
                    props: {
                        index: 0
                    },
                    formatters: [tableCellFormatter]
                }
            },
            {
                property: "actions",
                header: {
                    label: "",
                    props: {
                        index: 1,
                        rowSpan: 1,
                        colSpan: 1
                    },
                    formatters: [actionHeaderCellFormatter]
                },
                cell: {
                    props: {
                        index: 2
                    },
                    formatters: [
                        (value, { rowData }) => {
                            return [
                                <td key={rowData.sortName[0]}>
                                    <Button
                                        onClick={() => {
                                            this.handleVLVSortChange(
                                                rowData
                                            );
                                        }}
                                    >
                                        Remove
                                    </Button>
                                </td>
                            ];
                        }
                    ]
                }
            }
        ];
    }

    handleVLVSortChange(e) {
        // VLV index was removed from table
        let rows = this.state.sortRows;
        for (let i = 0; i < rows.length; i++) {
            if (rows[i].sortName == e.sortName) {
                rows.splice(i, 1);
                this.setState({
                    sortRows: rows
                });
                this.props.handleSortChange(rows);
                return;
            }
        }
    }

    handleTypeaheadChange(values) {
        const value = values.join(' ');
        this.setState({
            sortValue: value
        });
    }

    updateSorts() {
        let rows = this.state.sortRows;
        if (this.state.sortValue != "") {
            rows.push({sortName: this.state.sortValue});
            this.typeahead.getInstance().clear();
            this.setState({
                sortRows: rows,
                sortValue: ""
            });
            this.props.handleSortChange(rows);
        }
    }

    save() {
        // reset the rows, and call the save handler
        this.state.sortRows = [];
        this.props.saveHandler();
    }

    close() {
        this.state.sortRows = [];
        this.props.closeHandler();
    }

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            error,
            attrs
        } = this.props;
        let title;
        let nameInput;
        let base = "";
        let scope = "subtree";
        let filter = "";
        let sortTable;

        if (this.props.edit) {
            title = "Edit VLV Index";
            nameInput = <input className="ds-input-auto" type="text" value={this.props.vlvName} readOnly />;
            base = this.props.vlvBase;
            scope = this.props.vlvScope;
            filter = this.props.vlvFilter;
            if (this.props.vlvSortList !== undefined) {
                // {sortName: "value"},
                this.state.sortRows = this.props.vlvSortList;
            }
        } else {
            // Create new index
            // this.state.sortRows = [];
            title = "Create VLV Index";
            nameInput = <input className={error.vlvName ? "ds-input-auto-bad" : "ds-input-auto"} type="text" onChange={handleChange} id="vlvName" />;
        }

        let vlvscope =
            <Row>
                <Col sm={3}>
                    <ControlLabel>Search Scope</ControlLabel>
                </Col>
                <Col sm={9}>
                    <select defaultValue={scope}
                        onChange={this.props.handleChange} className="btn btn-default dropdown" id="vlvScope">
                        <option>subtree</option>
                        <option>one</option>
                        <option>base</option>
                    </select>
                </Col>
            </Row>;

        if (this.state.sortRows.length == 0) {
            sortTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No sort indexes"}]}
            />;
        } else {
            sortTable = <DSShortTable
                getColumns={this.getColumns}
                rowKey={"sortName"}
                rows={this.state.sortRows}
            />;
        }

        return (
            <Modal show={showModal} onHide={this.close}>
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
                            {title}
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>VLV Index Name</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    {nameInput}
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Search Base</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input className={error.vlvBase ? "ds-input-auto-bad" : "ds-input-auto"}
                                        onChange={handleChange} type="text" id="vlvBase" defaultValue={base} />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Search Filter</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input className={error.vlvFilter ? "ds-input-auto-bad" : "ds-input-auto"}
                                        onChange={handleChange} type="text" id="vlvFilter" defaultValue={filter} />
                                </Col>
                            </Row>
                            <p />
                            {vlvscope}
                            <hr />
                            <div>
                                <p />
                                <div>
                                    {sortTable}
                                    <p />
                                    <Typeahead
                                        multiple
                                        id="vlvsortindex"
                                        onChange={values => {
                                            this.handleTypeaheadChange(values);
                                        }}
                                        maxResults={1000}
                                        options={attrs}
                                        placeholder="Start typing attribute names to create a sort index"
                                        ref={(typeahead) => { this.typeahead = typeahead }}
                                    />
                                    <p />
                                    <button type="button" onClick={this.updateSorts}>Add Sort Index</button>
                                </div>
                            </div>
                            <hr />
                            <Row>
                                <Col sm={12}>
                                    <Checkbox className="ds-float-right" id="reindexVLV" onChange={handleChange}>
                                        Index VLV on Save
                                    </Checkbox>
                                </Col>
                            </Row>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={this.close}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={this.save}
                        >
                            Save VLV Index
                        </Button>
                    </Modal.Footer>
                </div>
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
    addNotification: noop,
    attrs: [],
    reload: noop,
};

AddVLVModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    handleSortChange: PropTypes.func,
    saveHandler: PropTypes.func,
    edit: PropTypes.bool,
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
    closeHandler: noop,
    handleChange: noop,
    handleSortChange: noop,
    saveHandler: noop,
    edit: false,
    error: {},
    attrs: [],
    vlvName: "",
    vlvBase: "",
    vlvScope: "",
    vlvFilter: "",
    vlvSortList: [],
};
