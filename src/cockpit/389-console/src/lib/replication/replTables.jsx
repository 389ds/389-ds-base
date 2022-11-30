import React from "react";
import {
    Button,
    Pagination,
    PaginationVariant,
    SearchInput,
    Spinner,
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
                { title: 'Name', transforms: [sortable] },
                { title: 'Host', transforms: [sortable] },
                { title: 'Port', transforms: [sortable] },
                { title: 'State', transforms: [sortable] },
                { title: 'Last Init Status', transforms: [sortable] },
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
    }

    componentDidMount() {
        // Deep copy the rows so we can handle sorting and searching
        this.setState({ page: this.props.page });
    }

    actions() {
        return [
            {
                title: 'Edit Agreement',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.edit(rowData.cells[0])
            },
            {
                title: 'Initialize Agreement',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.init(rowData.cells[0])
            },
            {
                title: 'Poke Agreement',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.poke(rowData.cells[0])
            },
            {
                title: 'Disable/Enable Agreement',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.enable(rowData.cells[0], rowData.cells[3])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Agreement',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.delete(rowData.cells[0], rowData.cells[3])
            },

        ];
    }

    convertStatus(msg) {
        if (msg == "Initialized") {
            return (
                <i>Initialized</i>
            );
        } else if (msg == "Not Initialized") {
            return (
                <i>Not Initialized</i>
            );
        } else if (msg == "Initializing") {
            return (
                <div>
                    <i>Initializing</i> <Spinner size="sm" />
                </div>
            );
        } else {
            return (
                <i>{msg}</i>
            );
        }
    }

    render() {
        // let rows = this.state.rows;
        const rows = [];
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        const rows_copy = JSON.parse(JSON.stringify(this.props.rows));

        // Refine rows to handle JSX objects
        for (const row of rows_copy) {
            rows.push({ cells: [row[0], row[1], row[2], row[3], { title: this.convertStatus(row[5]) }] });
        }

        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Replication Agreements' }];
            tableRows = [{ cells: ['No Agreements'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div className="ds-margin-top-xlg">
                <SearchInput
                    className="ds-margin-top-xlg"
                    placeholder='Search agreements'
                    value={this.props.value}
                    onChange={this.props.search}
                    onClear={(evt) => this.props.search('', evt)}
                />
                <Table
                    className="ds-margin-top-lg"
                    aria-label="agmt table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.props.sortBy}
                    onSort={this.props.sort}
                    actions={has_rows ? this.actions() : null}
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

class ManagerTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortBy: {},
            rows: [],
            columns: ['', ''],
        };

        this.onSort = this.onSort.bind(this);
        this.getDeleteButton = this.getDeleteButton.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const managerRow of this.props.rows) {
            rows.push({
                cells: [managerRow, { props: { textCenter: true }, title: this.getDeleteButton(managerRow) }]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Replication Managers'] }];
            columns = [{ title: '' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const rows = [];
        const sortedManagers = [...this.state.rows];

        // Sort the managers and build the new rows
        sortedManagers.sort();
        if (direction !== SortByDirection.asc) {
            sortedManagers.reverse();
        }

        for (const managerRow of sortedManagers) {
            rows.push({ cells: [managerRow.cells[0], { props: { textCenter: true }, title: this.getDeleteButton(managerRow.cells[0]) }] });
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

    getDeleteButton(name) {
        return (
            <a>
                <TrashAltIcon
                    className="ds-center"
                    onClick={() => {
                        this.props.confirmDelete(name);
                    }}
                    title="Delete Replication Manager"
                />
            </a>
        );
    }

    render() {
        return (
            <Table
                aria-label="manager table"
                cells={this.state.columns}
                rows={this.state.rows}
                variant={TableVariant.compact}
                sortBy={this.state.sortBy}
                onSort={this.onSort}
            >
                <TableHeader />
                <TableBody />
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
                { title: 'Replica ID', transforms: [sortable] },
                { title: 'Replica LDAP URL', transforms: [sortable] },
                { title: 'Max CSN', transforms: [sortable] },
                { title: '' },
            ],
        };

        this.onSort = this.onSort.bind(this);
        this.getCleanButton = this.getCleanButton.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const row of this.props.rows) {
            rows.push({
                cells: [row.rid, row.url, row.maxcsn, { props: { textCenter: true }, title: this.getCleanButton(row.rid) }]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ["No RUV's"] }];
            columns = [{ title: "Remote RUV's" }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
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

    getCleanButton(rid) {
        return (
            <Button
                variant="warning"
                onClick={() => {
                    this.props.confirmDelete(rid);
                }}
                title="Remove this RUV/Replica ID from all the Replicas."
            >
                Clean RUV
            </Button>
        );
    }

    render() {
        return (
            <div className="ds-margin-top">
                <Table
                    className="ds-margin-top"
                    aria-label="ruv table"
                    cells={this.state.columns}
                    rows={this.state.rows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
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
                { title: 'LDIF File', transforms: [sortable] },
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

    render() {
        const { columns, rows, sortBy } = this.state;

        return (
            <Table
                aria-label="ldif table"
                cells={columns}
                rows={rows}
                variant={TableVariant.compact}
                sortBy={sortBy}
                onSort={this.onSort}
                dropdownPosition="right"
                dropdownDirection="bottom"
            >
                <TableHeader />
                <TableBody />
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
