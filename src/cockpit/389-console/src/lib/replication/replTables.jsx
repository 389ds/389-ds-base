import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Pagination,
    SearchInput,
    Spinner,
} from '@patternfly/react-core';
import {
	Table,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
    ActionsColumn,
    SortByDirection,
} from '@patternfly/react-table';
import PropTypes from "prop-types";

const _ = cockpit.gettext;

class ReplAgmtTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("Name"), sortable: true },
                { title: _("Host"), sortable: true },
                { title: _("Port"), sortable: true },
                { title: _("State"), sortable: true },
                { title: _("Last Init Status"), sortable: true },
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
    }

    componentDidMount() {
        this.setState({ page: this.props.page });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Agreement"),
            onClick: () => this.props.edit(rowData[0])
        },
        {
            title: _("Initialize Agreement"),
            onClick: () => this.props.init(rowData[0])
        },
        {
            title: _("Poke Agreement"),
            onClick: () => this.props.poke(rowData[0])
        },
        {
            title: _("Disable/Enable Agreement"),
            onClick: () => this.props.enable(rowData[0], rowData[3])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Agreement"),
            onClick: () => this.props.delete(rowData[0], rowData[3])
        },
    ];

    convertStatus(msg) {
        if (msg === "Initialized") {
            return <i>{_("Initialized")}</i>;
        } else if (msg === "Not Initialized") {
            return <i>{_("Not Initialized")}</i>;
        } else if (msg === "Initializing") {
            return (
                <div>
                    <i>{_("Initializing")}</i> <Spinner size="sm" />
                </div>
            );
        }
        return <i>{msg}</i>;
    }

    render() {
        const rows = [];
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        const rows_copy = JSON.parse(JSON.stringify(this.props.rows));

        for (const row of rows_copy) {
            rows.push([row[0], row[1], row[2], row[3], this.convertStatus(row[5])]);
        }

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Replication Agreements") }];
            tableRows = [{ cells: [_("No Agreements")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div className="ds-margin-top-xlg">
                <SearchInput
                    className="ds-margin-top-xlg"
                    placeholder={_("Search agreements")}
                    value={this.props.value}
                    onChange={this.props.handleSearch}
                    onClear={(evt) => this.props.search(evt, '')}
                />
                <Table
                    className="ds-margin-top-lg"
                    aria-label="agmt table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.props.sortBy,
                                        onSort: this.props.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
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
                                {has_rows && (
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

class ManagerTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortBy: {},
            rows: [],
            columns: [
                {
                    title: 'Manager Name',
                    sortable: true
                }
            ],
        };

        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        let rows = [...this.props.rows];
        let columns = this.state.columns;

        if (this.props.rows.length === 0) {
            rows = [[_("No Replication Managers")]];
            columns = [{ title: '' }];
        }

        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, columnIndex, sortDirection) {
        const sortedRows = [...this.state.rows].sort((a, b) =>
            (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)
        );

        this.setState({
            sortBy: {
                index: columnIndex,
                direction: sortDirection
            },
            rows: sortDirection === 'asc' ? sortedRows : sortedRows.reverse()
        });
    }

    getRowActions = (row) => [
        {
            title: 'Change password',
            onClick: () => this.props.showEditManager(row)
        },
        {
            isSeparator: true
        },
        {
            title: 'Delete manager',
            onClick: () => this.props.confirmDelete(row)
        },
    ];

    render() {
        return (
            <Table
                aria-label="manager table"
                variant="compact"
            >
                <Thead>
                    <Tr>
                        {this.state.columns.map((column, columnIndex) => (
                            <Th
                                key={columnIndex}
                                sort={column.sortable ? {
                                    sortBy: this.state.sortBy,
                                    onSort: this.handleSort,
                                    columnIndex
                                } : undefined}
                                screenReaderText={column.screenReaderText}
                                textCenter={columnIndex === 1}
                            >
                                {column.title}
                            </Th>
                        ))}
                    </Tr>
                </Thead>
                <Tbody>
                    {this.state.rows.map((row, rowIndex) => (
                        <Tr key={rowIndex}>
                            <Td key={row}>
                                {row}
                            </Td>
                            {this.props.rows.length !== 0 &&
                                <Td isActionCell textCenter key={row +"action"}>
                                    <ActionsColumn
                                        items={this.getRowActions(row)}
                                    />
                                </Td>
                            }
                        </Tr>
                    ))}
                </Tbody>
            </Table>
        );
    }
}

class RUVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortBy: {},
            rows: [],
            columns: [
                { title: _("Replica ID"), sortable: true },
                { title: _("Replica LDAP URL"), sortable: true },
                { title: _("Max CSN"), sortable: true },
                { title: '', sortable: false, screenReaderText: _("Clean RUV") }
            ],
        };

        this.handleSort = this.handleSort.bind(this);
        this.getCleanButton = this.getCleanButton.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const row of this.props.rows) {
            rows.push([
                row.rid,
                row.url,
                row.maxcsn,
                this.getCleanButton(row.rid)
            ]);
        }
        if (rows.length === 0) {
            rows = [{ cells: [_("No RUV's")] }];
            columns = [{ title: _("Remote RUV's") }];
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
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    getCleanButton(rid) {
        return (
            <Button
                variant="warning"
                onClick={() => {
                    this.props.confirmDelete(rid);
                }}
                title={_("Remove this RUV/Replica ID from all the Replicas.")}
            >
                {_("Clean RUV")}
            </Button>
        );
    }

    render() {
        const tableRows = this.state.rows.map((row, rowIndex) => ({
            cells: Array.isArray(row) ? row : row.cells
        }));

        return (
            <div className="ds-margin-top">
                <Table
                    aria-label="ruv table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {this.state.columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
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
                                {row.cells.map((cell, cellIndex) => (
                                    <Td
                                        key={cellIndex}
                                        textCenter={cellIndex === row.cells.length - 1}
                                    >
                                        {cell}
                                    </Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
            </div>
        );
    }
}

class ReplicaLDIFTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("LDIF File"), sortable: true },
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
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse(),
            page: 1,
        });
    }

    render() {
        const { columns, rows, sortBy } = this.state;

        return (
            <Table
                aria-label="ldif table"
                variant="compact"
            >
                <Thead>
                    <Tr>
                        {columns.map((column, idx) => (
                            <Th
                                key={idx}
                                sort={column.sortable ? {
                                    sortBy: sortBy,
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
                    {rows.map((row, rowIndex) => (
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
                        </Tr>
                    ))}
                </Tbody>
            </Table>
        );
    }
}

ReplAgmtTable.propTypes = {
    rows: PropTypes.array,
    edit: PropTypes.func,
    poke: PropTypes.func,
    init: PropTypes.func,
    enable: PropTypes.func,
    delete: PropTypes.func,
};

ReplAgmtTable.defaultProps = {
    rows: [],
};

ManagerTable.propTypes = {
    rows: PropTypes.array,
    confirmDelete: PropTypes.func
};

ManagerTable.defaultProps = {
    rows: [],
};

RUVTable.propTypes = {
    rows: PropTypes.array,
    confirmDelete: PropTypes.func
};

RUVTable.defaultProps = {
    rows: [],
};

ReplicaLDIFTable.propTypes = {
    rows: PropTypes.array,
};

ReplicaLDIFTable.defaultProps = {
    rows: [],
};

export {
    ReplAgmtTable,
    ManagerTable,
    RUVTable,
    ReplicaLDIFTable,
};
