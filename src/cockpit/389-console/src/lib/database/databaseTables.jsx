import React from "react";
import {
    Button,
    Grid,
    GridItem,
    Pagination,
    PaginationVariant,
    SearchInput,
} from '@patternfly/react-core';
import {
    expandable,
    Table,
    TableHeader,
    TableBody,
    TableVariant,
    sortable,
    SortByDirection,
} from '@patternfly/react-table';
import TrashAltIcon from '@patternfly/react-icons/dist/js/icons/trash-alt-icon';
import ArrowRightIcon from '@patternfly/react-icons/dist/js/icons/arrow-right-icon';
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
            <a>
                <TrashAltIcon
                    className="ds-center"
                    onClick={() => {
                        this.props.deleteRef(name);
                    }}
                    title="Delete this referral"
                />
            </a>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const refRow of this.props.rows) {
            rows.push({
                cells: [refRow, { props: { textCenter: true }, title: this.getDeleteButton(refRow) }]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Referrals'] }];
            columns = [{ title: 'Referrals' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedRefs = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedRefs.sort();
        if (direction !== SortByDirection.asc) {
            sortedRefs.reverse();
        }
        for (const refRow of sortedRefs) {
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
        this.setState({ rows: [...this.props.rows] });
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
                    this.props.deleteIndex(rowData[0], rowData)
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
        const val = value.toLowerCase();
        for (const row of this.props.rows) {
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
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Indexes' }];
            tableRows = [{ cells: ['No Indexes'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
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
            <a>
                <TrashAltIcon
                    className="ds-center"
                    onClick={() => {
                        this.props.deleteAttr(name);
                    }}
                    title="Delete this attribute"
                />
            </a>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const attrRow of this.props.rows) {
            rows.push({
                cells: [attrRow, { props: { textCenter: true }, title: this.getDeleteButton(attrRow) }]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Attributes'] }];
            columns = [{ title: 'Encrypted Attribute' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedAttrs = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedAttrs.sort();
        if (direction !== SortByDirection.asc) {
            sortedAttrs.reverse();
        }
        for (const attrRow of sortedAttrs) {
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
        for (const ldifRow of this.props.rows) {
            rows.push({
                cells: [
                    ldifRow[0], ldifRow[1], ldifRow[2]
                ]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No LDIF files'] }];
            columns = [{ title: 'LDIF File' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedLDIF = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedLDIF.sort();
        if (direction !== SortByDirection.asc) {
            sortedLDIF.reverse();
        }
        for (const ldifRow of sortedLDIF) {
            rows.push({
                cells:
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
        for (const ldifRow of this.props.rows) {
            rows.push({
                cells: [ldifRow[0], ldifRow[3], ldifRow[1], ldifRow[2]]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No LDIF files'] }];
            columns = [{ title: 'LDIF File' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedLDIF = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedLDIF.sort();
        if (direction !== SortByDirection.asc) {
            sortedLDIF.reverse();
        }
        for (const ldifRow of sortedLDIF) {
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
        for (const bakRow of this.props.rows) {
            rows.push({
                cells: [bakRow[0], bakRow[1], bakRow[2]]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Backups'] }];
            columns = [{ title: 'Backups' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedBaks = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedBaks.sort();
        if (direction !== SortByDirection.asc) {
            sortedBaks.reverse();
        }
        for (const bakRow of sortedBaks) {
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
                    dropdownDirection="up"
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
        for (const pwpRow of this.props.rows) {
            rows.push({
                cells: [pwpRow[0], pwpRow[1], pwpRow[2]]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Local Policies'] }];
            columns = [{ title: 'Local Password Policies' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedPwp = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedPwp.sort();
        if (direction !== SortByDirection.asc) {
            sortedPwp.reverse();
        }
        for (const pwpRow of sortedPwp) {
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

class VLVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            noRows: true,
            columns: [
                {
                    title: 'Name',
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                {
                    title: 'Search Base',
                    transforms: [sortable],
                },
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
        this.onCollapse = this.onCollapse.bind(this);
    }

    onSort(_event, index, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows pairings into a sortable array based on the column indexes
        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                expandedRow: this.state.rows[idx + 1],
                1: this.state.rows[idx].cells[0],
                2: this.state.rows[idx].cells[1],
            });
        }

        // Sort the rows and build the new rows
        sorted_rows.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_rows.reverse();
        }
        for (const srow of sorted_rows) {
            rows.push({
                isOpen: false,
                cells: [
                    srow[1],
                    srow[2],
                ],
            });
            srow.expandedRow.parent = count; // reset parent idx
            rows.push(srow.expandedRow);
            count += 2;
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

    getScopeKey(scope) {
        const mapping = {
            2: 'subtree',
            1: 'one',
            0: 'base'
        };
        return mapping[scope];
    }

    getExpandedRow(row) {
        const sort_indexes = row.sorts.map((sort) => {
            let indexState;
            if (sort.attrs.vlvenabled[0] == "0") {
                // html 5 deprecated font ...
                indexState = <font size="2" color="#d01c8b"><b>Disabled</b></font>;
            } else {
                indexState = <font size="2" color="#4dac26"><b>Uses: </b>{sort.attrs.vlvuses[0]}</font>;
            }
            return (
                <GridItem key={sort.attrs.vlvsort[0]} className="ds-container">
                    <div className="ds-lower-field-md">
                        <ArrowRightIcon /> {sort.attrs.vlvsort[0]} ({indexState})
                    </div>
                    <div>
                        <Button
                            className="ds-left-margin"
                            onClick={() => {
                                this.props.deleteSortFunc(row.attrs.cn[0], sort.attrs.vlvsort[0]);
                            }}
                            id={row.attrs.cn[0]}
                            icon={<TrashAltIcon />}
                            variant="link"
                        >
                            Delete
                        </Button>
                    </div>
                </GridItem>
            );
        });

        return (
            <Grid>
                <GridItem className="ds-label" span={2}>
                    Search Base:
                </GridItem>
                <GridItem span={10}>
                    {row.attrs.vlvbase[0]}
                </GridItem>
                <GridItem className="ds-label" span={2}>
                    Search Filter:
                </GridItem>
                <GridItem span={10}>
                    {row.attrs.vlvfilter[0]}
                </GridItem>
                <GridItem className="ds-label" span={2}>
                    Scope:
                </GridItem>
                <GridItem span={10}>
                    {this.getScopeKey(row.attrs.vlvscope[0])}
                </GridItem>
                <GridItem className="ds-label" span={12}>
                    Sort Indexes:
                </GridItem>
                <div className="ds-margin-top ds-indent">
                    {sort_indexes}
                </div>
                <GridItem className="ds-label" span={1}>
                    <Button
                        className="ds-margin-top"
                        onClick={() => {
                            this.props.addSortFunc(row.attrs.cn[0]);
                        }}
                        variant="primary"
                    >
                        Create Sort Index
                    </Button>
                </GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;
        let noRows = true;

        for (const row of this.props.rows) {
            rows.push(
                {
                    isOpen: false,
                    cells: [row.attrs.cn[0], row.attrs.vlvbase[0]],
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
                },
            );
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No VLV Indexes'] }];
            columns = [{ title: 'VLV Indexes' }];
        } else {
            noRows = false;
        }
        this.setState({
            rows: rows,
            columns: columns,
            noRows: noRows,
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    actions() {
        return [
            {
                title: 'Reindex VLV',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.reindexFunc(rowData.cells[0])
            },
            {
                title: 'Delete VLV',
                onClick: (event, rowId, rowData, extra) => {
                    this.props.deleteFunc(rowData.cells[0]);
                }
            }
        ];
    }

    render() {
        const { perPage, page, sortBy, rows, columns } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);

        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

        return (
            <div className={(this.props.saving || this.props.updating) ? "ds-margin-top-lg ds-disabled" : "ds-margin-top-lg"}>
                <Table
                    className="ds-margin-top"
                    aria-label="vlv table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    onCollapse={this.onCollapse}
                    actions={!this.state.noRows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.state.rows.length / 2}
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

VLVTable.propTypes = {
    rows: PropTypes.array,
    deleteFunc: PropTypes.func,
    reindexFunc: PropTypes.func,
};

VLVTable.defaultProps = {
    rows: [],
};

PwpTable.propTypes = {
    rows: PropTypes.array,
    editPolicy: PropTypes.func,
    deletePolicy: PropTypes.func
};

PwpTable.defaultProps = {
    rows: [],
};

BackupTable.propTypes = {
    rows: PropTypes.array,
    confirmRestore: PropTypes.func,
    confirmDelete: PropTypes.func
};

BackupTable.defaultProps = {
    rows: [],
};

LDIFTable.propTypes = {
    rows: PropTypes.array,
    confirmImport: PropTypes.func,
};

LDIFTable.defaultProps = {
    rows: [],
};

LDIFManageTable.propTypes = {
    rows: PropTypes.array,
    confirmImport: PropTypes.func,
    confirmDelete: PropTypes.func
};

LDIFManageTable.defaultProps = {
    rows: [],
};

ReferralTable.propTypes = {
    rows: PropTypes.array,
    deleteRef: PropTypes.func
};

ReferralTable.defaultProps = {
    rows: [],
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
};

EncryptedAttrTable.propTypes = {
    deleteAttr: PropTypes.func,
    rows: PropTypes.array,
};

EncryptedAttrTable.defaultProps = {
    rows: [],
};

export {
    PwpTable,
    ReferralTable,
    IndexTable,
    EncryptedAttrTable,
    LDIFTable,
    LDIFManageTable,
    BackupTable,
    VLVTable,
};
