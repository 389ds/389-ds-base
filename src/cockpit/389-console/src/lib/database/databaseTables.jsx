import React from "react";
import {
    Button,
    Pagination,
    PaginationVariant,
    SearchInput,
    noop
} from '@patternfly/react-core';
import {
    Table,
    TableHeader,
    TableBody,
    TableVariant,
    sortable,
    SortByDirection,
} from '@patternfly/react-table';
import TrashAltIcon from '@patternfly/react-icons/dist/js/icons/trash-alt-icon';
import PropTypes from "prop-types";

class ReferralTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Referral', transforms: [sortable] },
                { props: { textCenter: true }, title: 'Delete Referral' },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.getDeleteButton = this.getDeleteButton.bind(this);
    }

    getDeleteButton(name) {
        return (
            <TrashAltIcon
                className="ds-center"
                onClick={() => {
                    this.props.deleteRef(name);
                }}
                title="Delete this referral"
            />
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (let refRow of this.props.rows) {
            rows.push({
                cells: [refRow, { props: { textCenter: true }, title: this.getDeleteButton(refRow) }]
            });
        }
        if (rows.length == 0) {
            rows = [{cells: ['No Referrals']}];
            columns = [{title: 'Referrals'}];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [];
        let sortedRefs = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedRefs.sort();
        if (direction !== SortByDirection.asc) {
            sortedRefs.reverse();
        }
        for (let refRow of sortedRefs) {
            rows.push({ cells: [refRow, { props: { textCenter: true }, title: this.getDeleteButton(refRow) }] });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="referral table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class IndexTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Attribute', transforms: [sortable] }, // name
                { title: 'Indexing Types', transforms: [sortable] }, // types
                { title: 'Matching Rules', transforms: [sortable] }, // matchingrules
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        // Copy the rows so we can handle sorting and searching
        this.setState({rows: [...this.props.rows]});
    }

    actions() {
        return [
            {
                title: 'Edit Index',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editIndex(rowData)
            },
            {
                title: 'Reindex',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.reindexIndex(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Index',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteIndex(rowData[0])
            }
        ];
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        let rows = [];
        let val = value.toLowerCase();
        for (let row of this.props.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2]]);
        }
        if (val == "") {
            // reset rows
            rows = [...this.props.rows];
        }
        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    render() {
        let rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{title: 'Indexes'}];
            tableRows = [{cells: ['No Indexes']}];
        } else {
            let startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div className="ds-margin-top-xlg">
                <SearchInput
                    className="ds-margin-top-xlg"
                    placeholder='Search indexes'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="glue table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows && this.props.editable ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class EncryptedAttrTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Encrypted Attribute', transforms: [sortable] },
                { props: { textCenter: true }, title: 'Delete Attribute' },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.getDeleteButton = this.getDeleteButton.bind(this);
    }

    getDeleteButton(name) {
        return (
            <TrashAltIcon
                className="ds-center"
                onClick={() => {
                    this.props.deleteAttr(name);
                }}
                title="Delete this attribute"
            />
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (let attrRow of this.props.rows) {
            rows.push({
                cells: [attrRow, { props: { textCenter: true }, title: this.getDeleteButton(attrRow) }]
            });
        }
        if (rows.length == 0) {
            rows = [{cells: ['No Attributes']}];
            columns = [{title: 'Encrypted Attribute'}];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [];
        let sortedAttrs = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedAttrs.sort();
        if (direction !== SortByDirection.asc) {
            sortedAttrs.reverse();
        }
        for (let attrRow of sortedAttrs) {
            rows.push({ cells: [attrRow, { props: { textCenter: true }, title: this.getDeleteButton(attrRow) }] });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="referral table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class LDIFTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'LDIF File', transforms: [sortable] },
                { title: 'Creation Date', transforms: [sortable] },
                { title: 'File Size', transforms: [sortable] },
                { title: '' }
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.getImportButton = this.getImportButton.bind(this);
    }

    getImportButton(name) {
        return (
            <Button
                variant="primary"
                onClick={() => {
                    this.props.confirmImport(name);
                }}
                title="Initialize the database with this LDIF file"
            >
                Import
            </Button>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (let ldifRow of this.props.rows) {
            rows.push({
                cells: [
                    ldifRow[0], ldifRow[1], ldifRow[2]
                ]
            });
        }
        if (rows.length == 0) {
            rows = [{cells: ['No LDIF files']}];
            columns = [{title: 'LDIF File'}];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [];
        let sortedLDIF = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedLDIF.sort();
        if (direction !== SortByDirection.asc) {
            sortedLDIF.reverse();
        }
        for (let ldifRow of sortedLDIF) {
            rows.push({ cells:
                [
                    ldifRow[0], ldifRow[1], ldifRow[2]
                ]
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'Import LDIF File',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.confirmImport(rowData.cells[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="ldif table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    actions={this.props.rows.length > 0 ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class LDIFManageTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'LDIF File', transforms: [sortable] },
                { title: 'Suffix', transforms: [sortable] },
                { title: 'Creation Date', transforms: [sortable] },
                { title: 'File Size', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (let ldifRow of this.props.rows) {
            rows.push({
                cells: [ldifRow[0], ldifRow[3], ldifRow[1], ldifRow[2]]
            });
        }
        if (rows.length == 0) {
            rows = [{cells: ['No LDIF files']}];
            columns = [{title: 'LDIF File'}];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [];
        let sortedLDIF = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedLDIF.sort();
        if (direction !== SortByDirection.asc) {
            sortedLDIF.reverse();
        }
        for (let ldifRow of sortedLDIF) {
            rows.push({
                cells: [ldifRow[0], ldifRow[3], ldifRow[1], ldifRow[2]]
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'Import LDIF',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.confirmImport(rowData.cells[0], rowData.cells[1])
            },
            {
                title: 'Delete LDIF',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.confirmDelete(rowData.cells[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        let hasRows = true;
        if (this.props.rows.length == 0) {
            hasRows = false;
        }
        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="manage ldif table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    actions={hasRows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class BackupTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Backup', transforms: [sortable] },
                { title: 'Creation Date', transforms: [sortable] },
                { title: 'Size', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (let bakRow of this.props.rows) {
            rows.push({
                cells: [bakRow[0], bakRow[1], bakRow[2]]
            });
        }
        if (rows.length == 0) {
            rows = [{cells: ['No Backups']}];
            columns = [{title: 'Backups'}];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [];
        let sortedBaks = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedBaks.sort();
        if (direction !== SortByDirection.asc) {
            sortedBaks.reverse();
        }
        for (let bakRow of sortedBaks) {
            rows.push({
                cells: [bakRow[0], bakRow[1], bakRow[2]]
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'Restore Backup',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.confirmRestore(rowData.cells[0])
            },
            {
                title: 'Delete Backup',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.confirmDelete(rowData.cells[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        let hasRows = true;
        if (this.props.rows.length == 0) {
            hasRows = false;
        }
        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="backup table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    actions={hasRows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class PwpTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Target DN', transforms: [sortable] },
                { title: 'Policy Type', transforms: [sortable] },
                { title: 'Database Suffix', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (let pwpRow of this.props.rows) {
            rows.push({
                cells: [pwpRow[0], pwpRow[1], pwpRow[2]]
            });
        }
        if (rows.length == 0) {
            rows = [{cells: ['No Local Policies']}];
            columns = [{title: 'Local Password Policies'}];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [];
        let sortedPwp = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedPwp.sort();
        if (direction !== SortByDirection.asc) {
            sortedPwp.reverse();
        }
        for (let pwpRow of sortedPwp) {
            rows.push({
                cells: [pwpRow[0], pwpRow[1], pwpRow[2]]
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'Edit Policy',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editPolicy(rowData.cells[0])
            },
            {
                title: 'Delete policy',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deletePolicy(rowData.cells[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        let hasRows = true;
        if (this.props.rows.length == 0) {
            hasRows = false;
        }
        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="pwp table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    actions={hasRows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

// Property types and defaults

PwpTable.propTypes = {
    rows: PropTypes.array,
    editPolicy: PropTypes.func,
    deletePolicy: PropTypes.func
};

PwpTable.defaultProps = {
    rows: [],
    editPolicy: noop,
    deletePolicy: noop
};

BackupTable.propTypes = {
    rows: PropTypes.array,
    confirmRestore: PropTypes.func,
    confirmDelete: PropTypes.func
};

BackupTable.defaultProps = {
    rows: [],
    confirmRestore: noop,
    confirmDelete: noop
};

LDIFTable.propTypes = {
    rows: PropTypes.array,
    confirmImport: PropTypes.func,
};

LDIFTable.defaultProps = {
    rows: [],
    confirmImport: noop
};

LDIFManageTable.propTypes = {
    rows: PropTypes.array,
    confirmImport: PropTypes.func,
    confirmDelete: PropTypes.func
};

LDIFManageTable.defaultProps = {
    rows: [],
    confirmImport: noop,
    confirmDelete: noop
};

ReferralTable.propTypes = {
    rows: PropTypes.array,
    deleteRef: PropTypes.func
};

ReferralTable.defaultProps = {
    rows: [],
    deleteRef: noop
};

IndexTable.propTypes = {
    editable: PropTypes.bool,
    rows: PropTypes.array,
    editIndex: PropTypes.func,
    reindexIndex: PropTypes.func,
    deleteIndex: PropTypes.func,
};

IndexTable.defaultProps = {
    editable: false,
    rows: [],
    editIndex: noop,
    reindexIndex: noop,
    deleteIndex: noop,
};

EncryptedAttrTable.propTypes = {
    deleteAttr: PropTypes.func,
    rows: PropTypes.array,
};

EncryptedAttrTable.defaultProps = {
    deleteAttr: noop,
    rows: [],
};

export {
    PwpTable,
    ReferralTable,
    IndexTable,
    EncryptedAttrTable,
    LDIFTable,
    LDIFManageTable,
    BackupTable
};
