import cockpit from "cockpit";
import React from "react";
import {
    Grid,
    GridItem,
    Pagination,
    PaginationVariant,
    SearchInput,
    Tooltip,
} from '@patternfly/react-core';
import {
    Table,
	SortByDirection,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
    ActionsColumn,
    ExpandableRowContent
} from '@patternfly/react-table';
import PropTypes from "prop-types";

const _ = cockpit.gettext;

class KeyTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            hasRows: false,
            columns: [
                { title: _("Cipher"), sortable: true },
                { title: _("Key Identifier"), sortable: true },
                { title: _("State"), sortable: true },
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
        let hasRows = true;

        for (const ServerKey of this.props.ServerKeys) {
            rows.push([
                ServerKey.attrs.cipher,
                ServerKey.attrs.key_id,
                ServerKey.attrs.state
            ]);
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Orphan keys")] }];
            columns = [{ title: _("Orphan keys") }];
            hasRows = false;
        }

        this.setState({
            rows,
            columns,
            hasRows
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Delete Key"),
            onClick: () => {
                if (rowData[1]) {
                    this.props.delKey(rowData[1]);
                }
            }
        }
    ];

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

    render() {
        const { perPage, page, sortBy, rows, columns, hasRows } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);

        return (
            <div className="ds-margin-top-lg">
                <Tooltip
                    content={
                        <div>
                            <p>
                                {_("An orphan key is a private key in the NSS DB for which there is NO cert with the corresponding public key. An orphan key is created during CSR creation, when the certificate associated with a CSR has been imported into the NSS DB its orphan state will be removed.")}
                                <br /><br />
                                {_("Make sure an orphan key is not associated with a submitted CSR before you delete it.")}
                            </p>
                        </div>
                    }
                >
                    <a className="ds-font-size-sm">{_("What is an orphan key?")}</a>
                </Tooltip>
                <Table 
                    className="ds-margin-top"
                    aria-label="orph key table"
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
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                {hasRows &&
                    <Pagination
                        itemCount={rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={perPage}
                        page={page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />
                }
            </div>
        );
    }
}

class CSRTable extends React.Component {
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
                { title: _("Subject DN"), sortable: true },
                { title: _("Subject Alternative Names"), sortable: true },
                { title: _("Modification Date"), sortable: true },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
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

    getActionsForRow = (rowData) => [
        {
            title: _("Delete CSR"),
            onClick: () => this.props.delCSR(rowData[0])
        },
        {
            title: _("View CSR"),
            onClick: () => this.props.viewCSR(rowData[0])
        }
    ];

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let hasRows = true;

        for (const ServerCSR of this.props.ServerCSRs) {
            rows.push([
                ServerCSR.attrs.name,
                ServerCSR.attrs.subject,
                ServerCSR.attrs.subject_alt_names.join(", "),
                ServerCSR.attrs.modified
            ]);
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Certificate Signing Requests")] }];
            columns = [{ title: _("Certificate Signing Requests") }];
            hasRows = false;
        }

        this.setState({
            rows,
            columns,
            hasRows,
        });
    }

    handleSearchChange(event, value) {
        const rows = [];

        for (const cert of this.props.ServerCSRs) {
            const val = value.toLowerCase();

            if (val !== "" && cert.attrs.name.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject_alt_names.join().toLowerCase().indexOf(val) === -1 &&
                cert.attrs.modified.toLowerCase().indexOf(val) === -1) {
                continue;
            }

            rows.push([
                cert.attrs.name,
                cert.attrs.subject,
                cert.attrs.subject_alt_names.join(", "),
                cert.attrs.modified
            ]);
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const { perPage, page, sortBy, rows, columns, hasRows } = this.state;
        const tableRows = rows.slice((page - 1) * perPage, page * perPage);

        return (
            <div className="ds-margin-top-lg">
                {hasRows &&
                    <SearchInput
                        placeholder={_("Search CSRs")}
                        value={this.state.value}
                        onChange={this.handleSearchChange}
                        onClear={(evt) => this.handleSearchChange(evt, '')}
                    />
                }
                <Table 
                    className="ds-margin-top"
                    aria-label="csr table"
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
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                {hasRows &&
                    <Pagination
                        itemCount={rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={perPage}
                        page={page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />
                }
            </div>
        );
    }
}

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
            hasRows: false,
            columns: [
                {
                    title: _("Nickname"),
                    sortable: true
                },
                { title: _("Subject DN"), sortable: true },
                { title: _("Expiration Date"), sortable: true },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
    }

    handleSort(_event, columnIndex, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows pairings into a sortable array
        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                expandedRow: this.state.rows[idx + 1],
                1: this.state.rows[idx].cells[0].content,
                2: this.state.rows[idx].cells[1].content,
                3: this.state.rows[idx].cells[2].content,
                issuer: this.state.rows[idx].issuer,
                flags: this.state.rows[idx].flags
            });
        }

        sorted_rows.sort((a, b) => (a[columnIndex + 1] > b[columnIndex + 1]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_rows.reverse();
        }

        for (const srow of sorted_rows) {
            rows.push({
                isOpen: false,
                cells: [
                    { content: srow[1] },
                    { content: srow[2] },
                    { content: srow[3] }
                ],
                issuer: srow.issuer,
                flags: srow.flags,
            });
            srow.expandedRow.parent = count;
            rows.push(srow.expandedRow);
            count += 2;
        }

        this.setState({
            sortBy: {
                index: columnIndex,
                direction
            },
            rows,
            page: 1,
        });
    }

    getExpandedRow(issuer, flags) {
        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>{_("Issuer DN:")}</GridItem>
                <GridItem span={9}><b>{issuer}</b></GridItem>
                <GridItem span={3}>{_("Trust Flags:")}</GridItem>
                <GridItem span={9}><b>{flags}</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;
        let hasRows = true;

        for (const cert of this.props.certs) {
            rows.push({
                isOpen: false,
                cells: [
                    { content: cert.attrs.nickname },
                    { content: cert.attrs.subject },
                    { content: cert.attrs.expires }
                ],
                issuer: cert.attrs.issuer,
                flags: cert.attrs.flags,
                originalData: cert.attrs  // Store the original data for expansion
            });
            count += 1;
        }
        if (rows.length === 0) {
            rows = [{ cells: [{ content: _("No Certificates") }] }];
            columns = [{ title: _("Certificates") }];
            hasRows = false;
        }
        this.setState({
            rows,
            columns,
            hasRows,
        });
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        const index = (this.state.perPage * (this.state.page - 1) * 2) + rowIndex;
        rows[index].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const cert of this.props.certs) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val !== "" && cert.attrs.nickname.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.issuer.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.expires.toLowerCase().indexOf(val) === -1) {
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
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Trust Flags"),
            onClick: () => this.props.editCert(rowData.cells[0].content, rowData.flags)
        },
        {
            title: _("Export Certificate"),
            onClick: () => this.props.exportCert(rowData.cells[0].content)
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Certificate"),
            onClick: () => this.props.delCert(rowData.cells[0].content)
        }
    ];

    render() {
        const { perPage, page, sortBy, rows, columns, hasRows } = this.state;
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = rows.slice(startIdx, startIdx + (perPage * 2));

        return (
            <div className="ds-margin-top-lg">
                {hasRows &&
                    <SearchInput
                        placeholder={_("Search Certificates")}
                        value={this.state.value}
                        onChange={this.handleSearchChange}
                        onClear={(evt) => this.handleSearchChange(evt, '')}
                    />}
                <Table 
                    aria-label="cert table"
                    variant='compact'
                >
                    <Thead>
                        <Tr>
                            <Th screenReaderText="Row expansion" />
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
                            <Th screenReaderText="Actions" />
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                        }}
                                    />
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {cell.content}
                                        </Td>
                                    ))}
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                </Tr>
                                {row.isOpen && (
                                    <Tr isExpanded={true}>
                                        <Td colSpan={columns.length + 2}>
                                            <ExpandableRowContent>
                                                {this.getExpandedRow(row.issuer, row.flags)}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                {hasRows &&
                    <Pagination
                        itemCount={this.state.rows.length / 2}
                        widgetId="pagination-options-menu-bottom"
                        perPage={perPage}
                        page={page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />}
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
            hasRows: false,
            columns: [
                { title: _("Issued By"), sortable: true },
                { title: _("Effective Date"), sortable: true },
                { title: _("Next Update"), sortable: true },
                { title: _("Type"), sortable: true },
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

    getActionsForRow = (rowData) => [
        {
            title: _("View CRL"),
            onClick: () => this.props.editConfig(rowData.cells[0], rowData.cells[1], rowData.credsBindpw, rowData.pwInteractive)
        },
        {
            title: _("Delete CRL"),
            onClick: () => this.props.deleteConfig(rowData.cells[0])
        }
    ];

    handleSort(_event, index, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows pairings into a sortable array
        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                1: this.state.rows[idx].cells[0],
                2: this.state.rows[idx].cells[1],
                3: this.state.rows[idx].cells[2],
                issuer: this.state.rows[idx].issuer,
                flags: this.state.rows[idx].flags,
                expandedRow: this.state.rows[idx + 1]
            });
        }

        // Sort and rebuild rows
        sorted_rows.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_rows.reverse();
        }
        
        for (const srow of sorted_rows) {
            rows.push({
                isOpen: false,
                cells: [srow[1], srow[2], srow[3]],
                issuer: srow.issuer,
                flags: srow.flags,
            });
            srow.expandedRow.parent = count;
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

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const cert of this.props.certs) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val !== "" && cert.attrs.nickname.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.issuer.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.expires.toLowerCase().indexOf(val) === -1) {
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
            rows,
            value,
            page: 1,
            hasRows: rows.length !== 0,
        });
    }

    render() {
        const tableRows = this.state.rows.slice(
            (this.state.page - 1) * this.state.perPage,
            this.state.page * this.state.perPage
        );

        return (
            <div className="ds-margin-top">
                <SearchInput
                    placeholder={_("Search CRL's")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    aria-label="CRL Table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {this.state.columns.map((column, idx) => (
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
                            {this.state.hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))}
                                    {this.state.hasRows && (
                                        <Td isActionCell>
                                            <ActionsColumn 
                                                items={this.getActionsForRow(row)}
                                            />
                                        </Td>
                                    )}
                                </Tr>
                                {row.isOpen && (
                                    <Tr isExpanded>
                                        <Td colSpan={this.state.columns.length + 1}>
                                            <ExpandableRowContent>
                                                {this.getExpandedRow(row.issuer, row.flags)}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                {this.state.hasRows && (
                    <Pagination
                        itemCount={this.state.rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={this.state.perPage}
                        page={this.state.page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />
                )}
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

CSRTable.propTypes = {
    ServerCSRs: PropTypes.array,
    delCSR: PropTypes.func,
    viewCSR: PropTypes.func,
};

CSRTable.defaultProps = {
    ServerCSRs: [],
};

KeyTable.propTypes = {
    ServerKeys: PropTypes.array,
    delKey: PropTypes.func,
};

KeyTable.defaultProps = {
    ServerKeys: [],
};
export {
    CertTable,
    CRLTable,
    CSRTable,
    KeyTable,
};
