import cockpit from "cockpit";
import React from "react";
import {
    Grid,
    GridItem,
    Pagination,
    PaginationVariant,
    SearchInput,
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

export class SASLTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            dropdownIsOpen: false,
            hasRows: true,
            columns: [
                {
                    title: _("Mapping Name"),
                    sortable: true
                },
                { title: _("Search Base"), sortable: true },
                { title: _("Priority"), sortable: true },
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

        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                expandedRow: this.state.rows[idx + 1],
                1: this.state.rows[idx].cells[0].content,
                2: this.state.rows[idx].cells[1].content,
                3: this.state.rows[idx].cells[2].content,
                regex: this.state.rows[idx].regex,
                filter: this.state.rows[idx].filter
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
                regex: srow.regex,
                filter: srow.filter,
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

    getExpandedRow(regex, filter) {
        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>{_("Regular Expression:")}</GridItem>
                <GridItem span={9}><b>{regex}</b></GridItem>
                <GridItem span={3}>{_("Search Filter:")}</GridItem>
                <GridItem span={9}><b>{filter}</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let hasRows = true;
        let count = 0;

        for (const row of this.props.rows) {
            rows.push({
                isOpen: false,
                cells: [
                    { content: row.cn[0] },
                    { content: row.nssaslmapbasedntemplate[0] },
                    { content: row.nssaslmappriority[0] }
                ],
                regex: row.nssaslmapregexstring[0],
                filter: row.nssaslmapfiltertemplate[0],
            });
            count += 1;
        }
        if (rows.length === 0) {
            rows = [{ cells: [{ content: _("No SASL Mappings") }] }];
            columns = [{ title: _("SASL Mappings") }];
            hasRows = false;
        }
        this.setState({
            rows,
            columns,
            hasRows
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

        for (const row of this.props.rows) {
            const val = value.toLowerCase();

            if (val !== "" && row.cn[0].toLowerCase().indexOf(val) === -1 &&
                row.nssaslmapbasedntemplate[0].toLowerCase().indexOf(val) === -1 &&
                row.nssaslmappriority[0].toLowerCase().indexOf(val) === -1) {
                continue;
            }

            rows.push({
                isOpen: false,
                cells: [
                    { content: row.cn[0] },
                    { content: row.nssaslmapbasedntemplate[0] },
                    { content: row.nssaslmappriority[0] }
                ],
                regex: row.nssaslmapregexstring[0],
                filter: row.nssaslmapfiltertemplate[0],
            });
            count += 1;
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Mapping"),
            onClick: () => this.props.editMapping(
                rowData.cells[0].content,
                rowData.regex,
                rowData.cells[1].content,
                rowData.filter,
                rowData.cells[2].content,
            )
        },
        {
            title: _("Delete Mapping"),
            onClick: () => this.props.deleteMapping(rowData.cells[0].content)
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
                        placeholder={_("Search Mappings")}
                        value={this.state.value}
                        onChange={this.handleSearchChange}
                        onClear={(evt) => this.handleSearchChange(evt, '')}
                    />}
                <Table 
                    aria-label="sasl table"
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
                                                {this.getExpandedRow(row.regex, row.filter)}
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

SASLTable.propTypes = {
    rows: PropTypes.array,
    editMapping: PropTypes.func,
    deleteMapping: PropTypes.func
};

SASLTable.defaultProps = {
    rows: [],
};
