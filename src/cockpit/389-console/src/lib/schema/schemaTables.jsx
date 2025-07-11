import cockpit from "cockpit";
import React from "react";
import {
    Grid,
    GridItem,
    Pagination,
    SearchInput,
    Spinner,
    Text,
    TextContent,
    TextVariants,
} from '@patternfly/react-core';
import {
	Table,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
    ExpandableRowContent,
    ActionsColumn,
	SortByDirection
} from '@patternfly/react-table';
import PropTypes from "prop-types";

const _ = cockpit.gettext;

class ObjectClassesTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                {
                    title: _("Objectclass Name"),
                    sortable: true
                },
                {
                    title: _("OID"),
                    sortable: true
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
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
    }

    handleSort(_event, columnIndex, direction) {
        const rows = [...this.state.rows];

        rows.sort((a, b) => (a.cells[columnIndex].content > b.cells[columnIndex].content) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            rows.reverse();
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

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        const index = (this.state.perPage * (this.state.page - 1)) + rowIndex;
        rows[index].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSearchChange(event, value) {
        const rows = [];
        const val = value.toLowerCase();

        for (const row of this.props.rows) {
            // Check for matches of all the parts
            if (val !== "" &&
                row.name[0].toLowerCase().indexOf(val) === -1 &&
                row.oid[0].toLowerCase().indexOf(val) === -1) {
                continue;
            }

            let user_defined = false;
            if (row.x_origin.length > 0 &&
                row.x_origin.indexOf("user defined") !== -1) {
                user_defined = true;
            }

            rows.push({
                isOpen: false,
                cells: [
                    { content: row.name[0] },
                    { content: row.oid[0] }
                ],
                disableActions: !user_defined,
                originalData: row
            });
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getExpandedRow(row) {
        const kinds = ['STRUCTURAL', 'ABSTRACT', 'AUXILIARY'];
        const desc = row.desc ? row.desc[0] : <i>{_("No description")}</i>;
        const x_org = row.x_origin.join(" ");

        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>Description:</GridItem>
                <GridItem span={9}><b>{desc}</b></GridItem>
                <GridItem span={3}>X-Origin:</GridItem>
                <GridItem span={9}><b>{x_org}</b></GridItem>
                <GridItem span={3}>Superior Objectclass:</GridItem>
                <GridItem span={9}><b>{row.sup[0]}</b></GridItem>
                <GridItem span={3}>Kind:</GridItem>
                <GridItem span={9}><b>{kinds[row.kind]}</b></GridItem>
                <GridItem span={3}>Requires Attributes:</GridItem>
                <GridItem span={9}><b>{row.must.join(", ")}</b></GridItem>
                <GridItem span={3}>Allowed Attributes:</GridItem>
                <GridItem span={9}><b>{row.may.join(", ")}</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        const rows = [];

        for (const row of this.props.rows) {
            let user_defined = false;
            if (row.x_origin.length > 0 &&
                row.x_origin.indexOf("user defined") !== -1) {
                user_defined = true;
            }

            rows.push({
                isOpen: false,
                cells: [
                    { content: row.name[0] },
                    { content: row.oid[0] }
                ],
                disableActions: !user_defined,
                originalData: row
            });
        }

        this.setState({
            rows,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Objectclass"),
            onClick: () => this.props.editModalHandler(rowData.cells[0].content)
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Objectclass"),
            onClick: () => this.props.deleteHandler(rowData.cells[0].content)
        }
    ];

    render() {
        const { perPage, page, sortBy, rows, columns } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);

        let content = (
            <div className="ds-center ds-margin-top-xlg">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Loading Objectclasses ...")}
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>
        );

        if (!this.props.loading) {
            content = (
                <div>
                    <Grid>
                        <GridItem span={3}>
                            <SearchInput
                                placeholder={_("Search Objectclasses")}
                                value={this.state.value}
                                onChange={this.handleSearchChange}
                                onClear={(evt) => this.handleSearchChange(evt, '')}
                            />
                        </GridItem>
                    </Grid>
                    <Table
                        aria-label="objectclasses table"
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
                                                isDisabled={row.disableActions}
                                            />
                                        </Td>
                                    </Tr>
                                    {row.isOpen && (
                                        <Tr isExpanded={true}>
                                            <Td colSpan={columns.length + 2}>
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

        return (
            <div className="ds-margin-top-lg">
                {content}
            </div>
        );
    }
}

ObjectClassesTable.propTypes = {
    rows: PropTypes.array,
    editModalHandler: PropTypes.func,
    deleteHandler: PropTypes.func,
};

ObjectClassesTable.defaultProps = {
    rows: [],
};

class AttributesTable extends React.Component {
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
                    title: _("Attribute Name"),
                    sortable: true
                },
                { title: _("OID"), sortable: true },
                { title: _("Syntax"), sortable: true },
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
                page: 1 // reset page back to 1
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
                not_user_defined: this.state.rows[idx].disableActions
            });
        }

        // Sort and rebuild rows
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
                disableActions: srow.not_user_defined
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

    getExpandedRow(row) {
        const desc = row.desc ? row.desc[0] : <i>{_("No description")}</i>;
        const x_org = row.x_origin.join(", ");
        const usage = ["userApplications", "directoryOperation", "distributedOperation", "dSAOperation"];
        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>{_("Description:")}</GridItem>
                <GridItem span={9}><b>{desc}</b></GridItem>
                <GridItem span={3}>{_("X-Origin:")}</GridItem>
                <GridItem span={9}><b>{x_org}</b></GridItem>
                <GridItem span={3}>{_("Aliases:")}</GridItem>
                <GridItem span={9}><b>{row.aliases ? row.aliases.join(", ") : ""}</b></GridItem>
                <GridItem span={3}>{_("Parent Attribute:")}</GridItem>
                <GridItem span={9}><b>{row.sup.join(", ")}</b></GridItem>
                <GridItem span={3}>{_("Read Only:")}</GridItem>
                <GridItem span={9}><b>{row.no_user_mod ? "Yes" : "No"}</b></GridItem>
                <GridItem span={3}>{_("Multivalued:")}</GridItem>
                <GridItem span={9}><b>{row.single_value ? "No" : "Yes"}</b></GridItem>
                <GridItem span={3}>{_("Usage:")}</GridItem>
                <GridItem span={9}><b>{usage[row.usage]}</b></GridItem>
                <GridItem span={3}>{_("Equality Matching Rules:")}</GridItem>
                <GridItem span={9}><b>{row.equality ? row.equality.join(", ") : ""}</b></GridItem>
                <GridItem span={3}>{_("Substring Matching Rules:")}</GridItem>
                <GridItem span={9}><b>{row.substr ? row.substr.join(", ") : ""}</b></GridItem>
                <GridItem span={3}>{_("Ordering Matching Rules:")}</GridItem>
                <GridItem span={9}><b>{row.ordering ? row.ordering.join(", ") : ""}</b></GridItem>
            </Grid>
        );
    }

    getOIDContent(oid) {
        for (const syntax of this.props.syntaxes) {
            if (oid == syntax.id) {
                return oid + " (" + syntax.label + ")";
            }
        }
        return oid + " (Unknown)";
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let noRows = false;

        for (const row of this.props.rows) {
            let user_defined = row.x_origin.includes("user defined");
            rows.push({
                isOpen: false,
                cells: [
                    { content: row.name[0] },
                    { content: row.oid[0] },
                    { content: this.getOIDContent(row.syntax[0]) }
                ],
                disableActions: !user_defined,
                originalData: row
            });
        }

        if (rows.length === 0) {
            noRows = true;
            rows = [{ cells: [{ content: _("No Attributes") }] }];
            columns = [{ title: _("Attributes") }];
        }

        this.setState({
            rows,
            columns,
            noRows,
        });
    }

    handleCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1)) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSearchChange(event, value) {
        const rows = [];
        const val = value.toLowerCase();
        let count = 0;

        for (const row of this.props.rows) {
            let user_defined = false;

            // Check for matches of all the parts
            if (val !== "" && row.name[0].toLowerCase().indexOf(val) === -1 &&
                row.oid[0].toLowerCase().indexOf(val) === -1 &&
                row.syntax[0].toLowerCase().indexOf(val) === -1) {
                // Not a match
                continue;
            }

            if (row.x_origin.length > 0 &&
                row.x_origin.indexOf("user defined") !== -1) {
                user_defined = true;
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [
                        { content: row.name[0] },
                        { content: row.oid[0] },
                        { content: this.getOIDContent(row.syntax[0]) },
                    ],
                    disableActions: !user_defined,
                    originalData: row
                },
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Attribute"),
            onClick: () => this.props.editModalHandler(rowData.cells[0].content)
        },
        {
            title: _("Delete Attribute"),
            onClick: () => this.props.deleteHandler(rowData.cells[0].content)
        }
    ];

    render() {
        const { perPage, page, sortBy, rows, columns } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);

        let content = (
            <div className="ds-center ds-margin-top-xlg">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Loading Attributes ...")}
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>
        );

        if (!this.props.loading) {
            content = (
                <div>
                    <Grid>
                        <GridItem span={3}>
                            <SearchInput
                                placeholder={_("Search Attributes")}
                                value={this.state.value}
                                onChange={this.handleSearchChange}
                                onClear={(evt) => this.handleSearchChange(evt, '')}
                            />
                        </GridItem>
                    </Grid>
                    <Table
                        aria-label="attributes table"
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
                                                isDisabled={row.disableActions}
                                            />
                                        </Td>
                                    </Tr>
                                    {row.isOpen && (
                                        <Tr isExpanded={true}>
                                            <Td colSpan={columns.length + 2}>
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

        return (
            <div className="ds-margin-top-lg">
                {content}
            </div>
        );
    }
}

AttributesTable.propTypes = {
    rows: PropTypes.array,
    editModalHandler: PropTypes.func,
    deleteHandler: PropTypes.func,
    loading: PropTypes.bool
};

AttributesTable.defaultProps = {
    rows: [],
    syntaxes: [],
    loading: false
};

class MatchingRulesTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("Matching Rule"), sortable: true },
                { title: _("OID"), sortable: true },
                { title: _("Syntax"), sortable: true },
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
            });
            rows.push({
                ...srow.expandedRow,
                parent: count
            });
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

    getExpandedRow(row) {
        let desc = _("No description");
        if (row.desc) {
            desc = row.desc[0];
        }
        return (
            <Grid>
                <GridItem offset={1} span={10}>
                    {desc}
                </GridItem>
            </Grid>
        );
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        const index = (this.state.perPage * (this.state.page - 1) * 2) + rowIndex;
        rows[index].isOpen = isExpanding;
        this.setState({ rows });
    }

    componentDidMount() {
        let rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            rows.push(
                {
                    isOpen: false,
                    cells: [
                        { content: row.name[0] },
                        { content: row.oid[0] },
                        { content: row.syntax[0] }
                    ],
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ content: this.getExpandedRow(row) }]
                },
            );
            count += 2;
        }
        if (rows.length === 0) {
            rows = [{ cells: [{ content: 'No Matching Rules' }] }];
            this.setState({
                columns: [{ title: 'Matching Rules' }]
            });
        }
        this.setState({ rows });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            const val = value.toLowerCase();
            let name = "";
            // Check for matches of all the parts
            if (row.names && row.names.length > 0) {
                name = row.name[0];
            }
            if (name.toLowerCase().indexOf(val) === -1 &&
                row.oid[0].toLowerCase().indexOf(val) === -1 &&
                row.syntax[0].toLowerCase().indexOf(val) === -1) {
                // Not a match
                continue;
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [
                        { content: name || <i>&lt;{_("No Name")}&gt;</i> },
                        { content: row.oid[0] },
                        { content: row.syntax[0] }
                    ],
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ content: this.getExpandedRow(row) }]
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

    render() {
        const { perPage, page, sortBy, rows, columns } = this.state;
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = rows.slice(startIdx, startIdx + (perPage * 2));

        // Filter out the expanded rows for the main table display
        const displayRows = tableRows.filter((row, index) => index % 2 === 0);
        const expandedContent = tableRows.filter((row, index) => index % 2 === 1);

        return (
            <div>
                <Grid>
                    <GridItem span={3}>
                        <SearchInput
                            placeholder={_("Search Matching Rules")}
                            value={this.state.value}
                            onChange={this.handleSearchChange}
                            onClear={(evt) => this.handleSearchChange(evt, '')}
                        />
                    </GridItem>
                </Grid>
                <Table
                    className="ds-margin-top"
                    aria-label="matching rules table"
                    variant="compact"
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
                        </Tr>
                    </Thead>
                    <Tbody>
                        {displayRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => this.handleCollapse(null, rowIndex * 2, !row.isOpen)
                                        }}
                                    />
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {cell.content}
                                        </Td>
                                    ))}
                                </Tr>
                                {row.isOpen && (
                                    <Tr isExpanded={true}>
                                        <Td colSpan={columns.length + 1}>
                                            <ExpandableRowContent>
                                                {expandedContent[rowIndex].cells[0].content}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={rows.length / 2}
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

MatchingRulesTable.propTypes = {
    rows: PropTypes.array
};

MatchingRulesTable.defaultProps = {
    rows: []
};

export { ObjectClassesTable, AttributesTable, MatchingRulesTable };
