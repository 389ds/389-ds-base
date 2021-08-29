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
            columns: [
                {
                    title: 'Mapping Name',
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                { title: 'Search Base', transforms: [sortable] },
                { title: 'Priority', transforms: [sortable] },
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
                regex: this.state.rows[idx].regex,
                filter: this.state.rows[idx].filter
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
                regex: srow.regex,
                filter: srow.filter,
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

    getExpandedRow(regex, filter) {
        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>Regular Expression:</GridItem>
                <GridItem span={9}><b>{regex}</b></GridItem>
                <GridItem span={3}>Search Filter:</GridItem>
                <GridItem span={9}><b>{filter}</b></GridItem>

            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;
        for (const row of this.props.rows) {
            rows.push(
                {
                    isOpen: false,
                    cells: [row.cn[0], row.nssaslmapbasedntemplate[0], row.nssaslmappriority[0]],
                    regex: row.nssaslmapregexstring[0],
                    filter: row.nssaslmapfiltertemplate[0],

                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row.nssaslmapregexstring[0], row.nssaslmapfiltertemplate[0]) }]
                },
            );
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No SASL Mappings'] }];
            columns = [{ title: 'SASL Mappings' }];
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

        for (const row of this.props.rows) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val != "" && row.cn[0].toLowerCase().indexOf(val) == -1 &&
                row.nssaslmapbasedntemplate[0].toLowerCase().indexOf(val) == -1 &&
                row.nssaslmappriority[0].toLowerCase().indexOf(val) == -1) {
                // Not a match
                continue;
            }

            rows.push(
                {
                    isOpen: false,
                    cells: [row.cn[0], row.nssaslmapbasedntemplate[0], row.nssaslmappriority[0]],
                    regex: row.nssaslmapregexstring[0],
                    filter: row.nssaslmapfiltertemplate[0],

                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row.nssaslmapregexstring[0], row.nssaslmapfiltertemplate[0]) }]
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
                title: 'Edit Mapping',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editMapping(
                        rowData.cells[0],
                        rowData.regex,
                        rowData.cells[1],
                        rowData.filter,
                        rowData.cells[2],
                    )
            },
            {
                title: 'Delete Mapping',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteMapping(rowData.cells[0])
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
                    placeholder='Search Mappings'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="sasl table"
                    cells={columns}
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

SASLTable.propTypes = {
    rows: PropTypes.array,
    editMapping: PropTypes.func,
    deleteMapping: PropTypes.func
};

SASLTable.defaultProps = {
    rows: [],
};
