import React from "react";
import {
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
import PropTypes from "prop-types";

class CertTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            dropdownIsOpen: false,
            columns: [
                {
                    title: 'Nickname',
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                { title: 'Subject DN', transforms: [sortable] },
                { title: 'Expiration Date', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
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
                3: this.state.rows[idx].cells[2],
                issuer: this.state.rows[idx].issuer,
                flags: this.state.rows[idx].flags
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
                    srow[1], srow[2], srow[3]
                ],
                issuer: srow.issuer,
                flags: srow.flags,
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

    getExpandedRow(issuer, flags) {
        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>Issuer DN:</GridItem>
                <GridItem span={9}><b>{issuer}</b></GridItem>
                <GridItem span={3}>Trust Flags:</GridItem>
                <GridItem span={9}><b>{flags}</b></GridItem>

            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;

        for (const cert of this.props.certs) {
            rows.push(
                {
                    isOpen: false,
                    cells: [cert.attrs.nickname, cert.attrs.subject, cert.attrs.expires],
                    issuer: cert.attrs.issuer,
                    flags: cert.attrs.flags,

                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(cert.attrs.issuer, cert.attrs.flags) }]
                },
            );
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Certificates'] }];
            columns = [{ title: 'Certificates' }];
        }
        this.setState({
            rows: rows,
            columns: columns
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

    onSearchChange(value, event) {
        const rows = [];
        let count = 0;

        for (const cert of this.props.certs) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val != "" && cert.attrs.nickname.toLowerCase().indexOf(val) == -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) == -1 &&
                cert.attrs.issuer.toLowerCase().indexOf(val) == -1 &&
                cert.attrs.expires.toLowerCase().indexOf(val) == -1) {
                // Not a match
                continue;
            }

            rows.push(
                {
                    isOpen: false,
                    cells: [cert.attrs.nickname, cert.attrs.subject, cert.attrs.expires],
                    issuer: cert.attrs.issuer,
                    flags: cert.attrs.flags,

                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(cert.attrs.issuer, cert.attrs.flags) }]
                },
            );
            count += 2;
        }

        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'Edit Trust Flags',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editCert(rowData.cells[0], rowData.flags)
            },
            {
                title: 'Delete Certificate',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.delCert(rowData.cells[0])
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
            <div className="ds-margin-top-lg">
                <SearchInput
                    placeholder='Search Certificates'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="cert table"
                    cells={columns}
                    key={tableRows}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    onCollapse={this.onCollapse}
                    actions={tableRows.length > 0 ? this.actions() : null}
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

// Future - https://github.com/389ds/389-ds-base/issues/3548
class CRLTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            dropdownIsOpen: false,
            columns: [
                { title: 'Issued By', transforms: [sortable] },
                { title: 'Effective Date', transforms: [sortable] },
                { title: 'Next Update', transforms: [sortable] },
                { title: 'Type', transforms: [sortable] },
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

    onSort(_event, index, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows pairings into a sortable array based on the column indexes
        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                1: this.state.rows[idx].cells[0],
                2: this.state.rows[idx].cells[1],
                3: this.state.rows[idx].cells[2],
                issuer: this.state.rows[idx].issuer,
                flags: this.state.rows[idx].flags
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
                    srow[1], srow[2], srow[3]
                ],
                issuer: srow.issuer,
                flags: srow.flags,
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

    onSearchChange(value, event) {
        const rows = [];
        let count = 0;

        for (const cert of this.props.certs) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val != "" && cert.attrs.nickname.toLowerCase().indexOf(val) == -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) == -1 &&
                cert.attrs.issuer.toLowerCase().indexOf(val) == -1 &&
                cert.attrs.expires.toLowerCase().indexOf(val) == -1) {
                // Not a match
                continue;
            }

            rows.push(
                {
                    isOpen: false,
                    cells: [cert.attrs.nickname, cert.attrs.subject, cert.attrs.expires],
                    issuer: cert.attrs.issuer,
                    flags: cert.attrs.flags,

                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(cert.attrs.issuer, cert.attrs.flags) }]
                },
            );
            count += 2;
        }

        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'View CRL',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData.cells[0], rowData.cells[1], rowData.credsBindpw, rowData.pwInteractive)
            },
            {
                title: 'Delete CRL',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData.cells[0])
            }
        ];
    }

    render() {
        const has_rows = false; // TODO
        return (
            <div className="ds-margin-top">
                <SearchInput
                    placeholder="Search CRL's"
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    variant={TableVariant.compact} aria-label="Cred Table"
                    sortBy={this.sortBy} onSort={this.onSort} cells={this.state.columns}
                    rows={this.state.rows}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.state.rows.length}
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

// Props and defaults

CertTable.propTypes = {
    // serverId: PropTypes.string,
    certs: PropTypes.array,
    editCert: PropTypes.func,
    delCert: PropTypes.func,
};

CertTable.defaultProps = {
    // serverId: "",
    certs: [],
};

export {
    CertTable,
    CRLTable
};
