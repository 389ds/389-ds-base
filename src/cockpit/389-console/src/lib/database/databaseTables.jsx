import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Grid,
    GridItem,
    Pagination,
    SearchInput,
} from '@patternfly/react-core';
import {
	Table,
	Thead,
	Tr,
	Th,
	Tbody,
	Td,
	ActionsColumn,
	ExpandableRowContent,
	SortByDirection
} from '@patternfly/react-table';
import { TrashAltIcon } from '@patternfly/react-icons/dist/js/icons/trash-alt-icon';
import { ArrowRightIcon } from '@patternfly/react-icons/dist/js/icons/arrow-right-icon';
import PropTypes from "prop-types";

const _ = cockpit.gettext;

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
                { title: _("Referral"), sortable: true },
                { title: _("Delete Referral") },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
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
                    title={_("Delete this referral")}
                />
            </a>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const refRow of this.props.rows) {
            rows.push([refRow, this.getDeleteButton(refRow)]);
        }
        if (rows.length === 0) {
            rows = [[_("No Referrals")]];
            columns = [{ title: _("Referrals") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedRefs = [...this.props.rows];
        
        sortedRefs.sort();
        if (direction !== 'asc') {
            sortedRefs.reverse();
        }
        
        const rows = sortedRefs.map(refRow => [
            refRow,
            this.getDeleteButton(refRow)
        ]);

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const displayRows = rows.slice(startIdx, startIdx + perPage);

        return (
            <div className="ds-margin-top-lg">
                <Table
                    aria-label="referral table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {displayRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td 
                                        key={cellIndex}
                                        textCenter={cellIndex === 1}
                                    >
                                        {cell}
                                    </Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Attribute"), sortable: true },
                { title: _("Indexing Types"), sortable: true },
                { title: _("Matching Rules"), sortable: true }
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
    }

    componentDidMount() {
        this.setState({ rows: [...this.props.rows] });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = [...this.props.rows];
        } else {
            for (const row of this.props.rows) {
                if (row[0].toLowerCase().includes(val) ||
                    row[1].toLowerCase().includes(val) ||
                    row[2].toLowerCase().includes(val)) {
                    rows.push([row[0], row[1], row[2]]);
                }
            }
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        const getActionsForRow = (rowData) => [
            {
                title: _("Edit Index"),
                onClick: () => this.props.editIndex(rowData)
            },
            {
                title: _("Reindex"),
                onClick: () => this.props.reindexIndex(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: _("Delete Index"),
                onClick: () => this.props.deleteIndex(rowData[0], rowData)
            }
        ];

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Indexes") }];
            tableRows = [{ cells: [_("No Indexes")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div className="ds-margin-top-xlg">
                <SearchInput
                    className="ds-margin-top-xlg"
                    placeholder={_("Search indexes")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    aria-label="index table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && this.props.editable && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    // Handle array-type rows
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {Array.isArray(cell) && cell.length === 0 ? 
                                                '' : // Handle empty arrays
                                                String(cell) // Convert any value to string
                                            }
                                        </Td>
                                    ))
                                ) : (
                                    // Handle object-type rows (for the "No Indexes" case)
                                    row.cells && row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && this.props.editable && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Encrypted Attribute"), sortable: true },
                { title: _("Delete Attribute") },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
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
                    title={_("Delete this attribute")}
                />
            </a>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const attrRow of this.props.rows) {
            rows.push([attrRow, this.getDeleteButton(attrRow)]);
        }
        if (rows.length === 0) {
            rows = [[_("No Attributes")]];
            columns = [{ title: _("Encrypted Attribute") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedAttrs = [...this.props.rows];

        sortedAttrs.sort();
        if (direction !== 'asc') {
            sortedAttrs.reverse();
        }
        
        const rows = sortedAttrs.map(attrRow => [
            attrRow,
            this.getDeleteButton(attrRow)
        ]);

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const displayRows = rows.slice(startIdx, startIdx + perPage);

        return (
            <div className="ds-margin-top-lg">
                <Table
                    aria-label="encrypted attributes table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {displayRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td 
                                        key={cellIndex}
                                        textCenter={cellIndex === 1}
                                    >
                                        {cell}
                                    </Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("LDIF File"), sortable: true },
                { title: _("Creation Date"), sortable: true },
                { title: _("File Size"), sortable: true },
                { title: _("Actions"), screenReaderText: _("LDIF file actions") }
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.getImportButton = this.getImportButton.bind(this);
    }

    getImportButton(name) {
        return (
            <Button
                variant="primary"
                onClick={() => {
                    this.props.confirmImport(name);
                }}
                title={_("Initialize the database with this LDIF file")}
            >
                {_("Import")}
            </Button>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const ldifRow of this.props.rows) {
            rows.push(ldifRow);
        }
        if (rows.length === 0) {
            rows = [[_("No LDIF files")]];
            columns = [{ title: _("LDIF File") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === 'asc' ? sortedRows : sortedRows.reverse(),
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Import LDIF File"),
            onClick: () => this.props.confirmImport(rowData[0])
        }
    ];

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);
        const hasRows = this.props.rows.length > 0;

        return (
            <div className="ds-margin-top-lg">
                <Table
                    aria-label="ldif table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={hasRows && column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                    screenReaderText={column.screenReaderText}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    <Td>{row}</Td>
                                )}
                                {/* Only render the action column if we have rows */}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("LDIF File"), sortable: true },
                { title: _("Suffix"), sortable: true },
                { title: _("Creation Date"), sortable: true },
                { title: _("File Size"), sortable: true },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const ldifRow of this.props.rows) {
            rows.push([ldifRow[0], ldifRow[3], ldifRow[1], ldifRow[2]]);
        }
        if (rows.length === 0) {
            rows = [{ cells: [_("No LDIF files")] }];
            columns = [{ title: _("LDIF File") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedLDIF = [...this.state.rows];
        
        sortedLDIF.sort((a, b) => {
            const aValue = Array.isArray(a) ? a[index] : a.cells[index];
            const bValue = Array.isArray(b) ? b[index] : b.cells[index];
            return aValue < bValue ? -1 : aValue > bValue ? 1 : 0;
        });

        if (direction !== SortByDirection.asc) {
            sortedLDIF.reverse();
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: sortedLDIF,
            page: 1,
        });
    }

    getActions(rowData) {
        return [
            {
                title: _("Import LDIF"),
                onClick: () => this.props.confirmImport(rowData[0], rowData[1])
            },
            {
                title: _("Delete LDIF"),
                onClick: () => this.props.confirmDelete(rowData[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const hasRows = this.props.rows.length > 0;
        
        // Calculate pagination
        const startIdx = (perPage * page) - perPage;
        const tableRows = [...rows].splice(startIdx, perPage);

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="manage ldif table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActions(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Backup"), sortable: true },
                { title: _("Creation Date"), sortable: true },
                { title: _("Size"), sortable: true },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const bakRow of this.props.rows) {
            rows.push([bakRow[0], bakRow[1], bakRow[2]]);
        }
        if (rows.length === 0) {
            rows = [[_("No Backups")]];
            columns = [{ title: _("Backups") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedBaks = [...this.state.rows];
        
        sortedBaks.sort((a, b) => {
            const aValue = Array.isArray(a) ? a[index] : a[0];
            const bValue = Array.isArray(b) ? b[index] : b[0];
            return aValue < bValue ? -1 : aValue > bValue ? 1 : 0;
        });

        if (direction !== 'asc') {
            sortedBaks.reverse();
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: sortedBaks,
            page: 1,
        });
    }

    getActions(rowData) {
        return [
            {
                title: _("Restore Backup"),
                onClick: () => this.props.confirmRestore(rowData[0])
            },
            {
                title: _("Delete Backup"),
                onClick: () => this.props.confirmDelete(rowData[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const hasRows = this.props.rows.length > 0;
        
        // Calculate pagination
        const startIdx = (perPage * page) - perPage;
        const tableRows = [...rows].splice(startIdx, perPage);

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="backup table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActions(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Target DN"), sortable: true },
                { title: _("Policy Type"), sortable: true },
                { title: _("Database Suffix"), sortable: true },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const pwpRow of this.props.rows) {
            rows.push([pwpRow[0], pwpRow[1], pwpRow[2]]);
        }
        if (rows.length === 0) {
            rows = [{ cells: [_("No Local Policies")] }];
            columns = [{ title: _("Local Password Policies") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedPwp = [...this.state.rows];
        
        sortedPwp.sort((a, b) => {
            const aValue = Array.isArray(a) ? a[index] : a.cells[index];
            const bValue = Array.isArray(b) ? b[index] : b.cells[index];
            return aValue < bValue ? -1 : aValue > bValue ? 1 : 0;
        });

        if (direction !== SortByDirection.asc) {
            sortedPwp.reverse();
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: sortedPwp,
            page: 1,
        });
    }

    getActions(rowData) {
        return [
            {
                title: _("Edit Policy"),
                onClick: () => this.props.editPolicy(rowData[0])
            },
            {
                title: _("Delete policy"),
                onClick: () => this.props.deletePolicy(rowData[0])
            },
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const hasRows = this.props.rows.length > 0;
        
        // Calculate pagination
        const startIdx = (perPage * page) - perPage;
        const tableRows = [...rows].splice(startIdx, perPage);

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="pwp table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn
                                            isDisabled={row[1] === "Unknown policy type"}
                                            items={this.getActions(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                    title: _("Name"),
                    sortable: true,
                },
                {
                    title: _("Search Base"),
                    sortable: true,
                },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
    }

    handleSort(_event, index, direction) {
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
            rows,
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
            if (sort.attrs.vlvenabled[0] === "0") {
                // html 5 deprecated font ...
                indexState = <font size="2" color="#d01c8b"><b>{_("Disabled")}</b></font>;
            } else {
                indexState = <font size="2" color="#4dac26"><b>{_("Uses: ")}</b>{sort.attrs.vlvuses[0]}</font>;
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
                            {_("Delete")}
                        </Button>
                    </div>
                </GridItem>
            );
        });

        return (
            <Grid>
                <GridItem className="ds-label" span={2}>
                    {_("Search Base:")}
                </GridItem>
                <GridItem span={10}>
                    {row.attrs.vlvbase[0]}
                </GridItem>
                <GridItem className="ds-label" span={2}>
                    {_("Search Filter:")}
                </GridItem>
                <GridItem span={10}>
                    {row.attrs.vlvfilter[0]}
                </GridItem>
                <GridItem className="ds-label" span={2}>
                    {_("Scope:")}
                </GridItem>
                <GridItem span={10}>
                    {this.getScopeKey(row.attrs.vlvscope[0])}
                </GridItem>
                <GridItem className="ds-label" span={12}>
                    {_("Sort Indexes:")}
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
                        {_("Create Sort Index")}
                    </Button>
                </GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let noRows = true;

        for (const row of this.props.rows) {
            rows.push({
                isOpen: false,
                cells: [row.attrs.cn[0], row.attrs.vlvbase[0]],
                originalData: row // Store original data for expanded content
            });
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No VLV Indexes")] }];
            this.setState({
                columns: [{ title: _("VLV Indexes") }]
            });
        } else {
            noRows = false;
        }

        this.setState({
            rows,
            noRows,
        });
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        rows[rowIndex].isOpen = isExpanding;
        this.setState({ rows });
    }

    getActions(rowData) {
        return [
            {
                title: _("Reindex VLV"),
                onClick: () => this.props.reindexFunc(rowData.cells[0])
            },
            {
                title: _("Delete VLV"),
                onClick: () => this.props.deleteFunc(rowData.cells[0])
            }
        ];
    }

    render() {
        const { perPage, page, sortBy, rows, columns, noRows } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, perPage);

        return (
            <div className={(this.props.saving || this.props.updating) ? "ds-margin-top-lg ds-disabled" : "ds-margin-top-lg"}>
                <Table 
                    aria-label="vlv table"
                    variant='compact'
                >
                    <Thead>
                        <Tr>
                            {!noRows && <Th screenReaderText="Row expansion" />}
                            {columns.map((column, columnIndex) => (
                                <Th 
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {!noRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {!noRows && (
                                        <Td 
                                            expand={{
                                                rowIndex,
                                                isExpanded: row.isOpen,
                                                onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                            }}
                                        />
                                    )}
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))}
                                    {!noRows && (
                                        <Td isActionCell>
                                            <ActionsColumn 
                                                items={this.getActions(row)}
                                            />
                                        </Td>
                                    )}
                                </Tr>
                                {row.isOpen && row.originalData && (
                                    <Tr isExpanded={true}>
                                        <Td />
                                        <Td 
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            <ExpandableRowContent>
                                                {this.getExpandedRow(row.originalData)}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
