import React from "react";
import {
    PaginationRow,
    paginate,
    Table,
    customHeaderFormattersDefinition,
    defaultSortingOrder,
    TABLE_SORT_DIRECTION,
    PAGINATION_VIEW
} from "patternfly-react";
import PropTypes from "prop-types";
import { orderBy } from "lodash";
import * as sort from "sortabular";
import * as resolve from "table-resolver";
import { compose } from "recompose";
import { searchFilter } from "./tools.jsx";
import CustomTableToolbar from "./customTableToolbar.jsx";
import "../css/ds.css";

class DSTable extends React.Component {
    // props:
    // getColumns={this.getColumns}
    // fieldsToSearch={this.state.fieldsToSearch}
    // searchField={this.state.searchField}
    // rowKey
    // rows: table rows
    //
    constructor(props) {
        super(props);

        // Point the transform to your sortingColumns. React state can work for this purpose
        // but you can use a state manager as well.
        const getSortingColumns = () => this.state.sortingColumns || {};
        let pagination_list = [6, 12, 24, 48, 96];
        let pagination_per_page = 12;
        if (this.props.toolBarPagination !== undefined) {
            pagination_list = this.props.toolBarPagination;
        }
        if (this.props.toolBarPaginationPerPage !== undefined) {
            pagination_per_page = this.props.toolBarPaginationPerPage;
        }

        const sortableTransform = sort.sort({
            getSortingColumns,
            onSort: selectedColumn => {
                this.setState({
                    sortingColumns: sort.byColumn({
                        sortingColumns: this.state.sortingColumns,
                        sortingOrder: defaultSortingOrder,
                        selectedColumn
                    })
                });
            },
            // Use property or index dependening on the sortingColumns structure specified
            strategy: sort.strategies.byProperty
        });

        const sortingFormatter = sort.header({
            sortableTransform,
            getSortingColumns,
            strategy: sort.strategies.byProperty
        });

        // Update column formatter stuff
        let customColumns = this.props.getColumns();
        for (let customColumn of customColumns) {
            if (customColumn.header.formatters.length == 0) {
                customColumn.header.formatters = [sortingFormatter];
                customColumn.header.transforms = [sortableTransform];
            }
        }

        // Enables our custom header formatters extensions to reactabular
        this.customHeaderFormatters = customHeaderFormattersDefinition;

        this.handleSearchValueChange = this.handleSearchValueChange.bind(this);
        this.totalPages = this.totalPages.bind(this);
        this.onPageInput = this.onPageInput.bind(this);
        this.onSubmit = this.onSubmit.bind(this);
        this.setPage = this.setPage.bind(this);
        this.onPerPageSelect = this.onPerPageSelect.bind(this);
        this.onFirstPage = this.onFirstPage.bind(this);
        this.onPreviousPage = this.onPreviousPage.bind(this);
        this.onNextPage = this.onNextPage.bind(this);
        this.onLastPage = this.onLastPage.bind(this);
        this.currentRows = this.currentRows.bind(this);
        this.filteredSearchedRows = this.filteredSearchedRows.bind(this);

        this.state = {
            searchFilterValue: "",
            fieldsToSearch: this.props.fieldsToSearch,
            // Sort the first column in an ascending way by default.
            sortingColumns: {
                name: {
                    direction: TABLE_SORT_DIRECTION.ASC,
                    position: 0
                }
            },
            columns: customColumns,

            // pagination default states
            pagination: {
                page: 1,
                perPage: pagination_per_page,
                perPageOptions: pagination_list
            },

            // page input value
            pageChangeValue: 1
        };
    }

    handleSearchValueChange(event) {
        this.setState({ searchFilterValue: event.target.value });
    }

    totalPages() {
        const { rows } = this.props;
        const { perPage } = this.state.pagination;
        return Math.ceil(rows.length / perPage);
    }

    onPageInput(e) {
        this.setState({ pageChangeValue: e.target.value });
    }

    onSubmit() {
        this.setPage(this.state.pageChangeValue);
    }

    setPage(value) {
        const page = Number(value);
        if (
            !Number.isNaN(value) &&
            value !== "" &&
            page > 0 &&
            page <= this.totalPages()
        ) {
            let newPaginationState = Object.assign({}, this.state.pagination);
            newPaginationState.page = page;
            this.setState({
                pagination: newPaginationState,
                pageChangeValue: page
            });
        }
    }

    onPerPageSelect(eventKey, e) {
        let newPaginationState = Object.assign({}, this.state.pagination);
        newPaginationState.perPage = eventKey;
        newPaginationState.page = 1;
        this.setState({ pagination: newPaginationState });
    }

    onFirstPage() {
        this.setPage(1);
    }

    onPreviousPage() {
        if (this.state.pagination.page > 1) {
            this.setPage(this.state.pagination.page - 1);
        }
    }

    onNextPage() {
        const { page } = this.state.pagination;
        if (page < this.totalPages()) {
            this.setPage(this.state.pagination.page + 1);
        }
    }

    onLastPage() {
        const { page } = this.state.pagination;
        const totalPages = this.totalPages();
        if (page < totalPages) {
            this.setPage(totalPages);
        }
    }

    currentRows(filteredRows) {
        const { sortingColumns, columns, pagination } = this.state;
        return compose(
            paginate(pagination),
            sort.sorter({
                columns: columns,
                sortingColumns,
                sort: orderBy,
                strategy: sort.strategies.byProperty
            })
        )(filteredRows);
    }

    filteredSearchedRows() {
        const { rows } = this.props;
        const { fieldsToSearch, searchFilterValue } = this.state;
        if (searchFilterValue) {
            return searchFilter(searchFilterValue, fieldsToSearch, rows);
        }
        return rows;
    }

    render() {
        const {
            columns,
            pagination,
            sortingColumns,
            pageChangeValue
        } = this.state;
        const filteredRows = this.filteredSearchedRows();
        const sortedPaginatedRows = this.currentRows(filteredRows);
        let dsSearchBar =
            <CustomTableToolbar
                searchFilterValue={this.state.searchFilterValue}
                handleValueChange={this.handleSearchValueChange}
                children={this.props.toolBarChildren}
                className={this.props.toolBarClassName}
                placeholder={this.props.toolBarPlaceholder}
                modelToSearch={this.props.toolBarSearchField}
                loading={this.props.toolBarLoading}
                disableLoadingSpinner={this.props.toolBarDisableLoadingSpinner}
            />;
        if (this.props.noSearchBar) {
            dsSearchBar = "";
        }

        return (
            <div>
                {dsSearchBar}
                <Table.PfProvider
                    className="display ds-db-table"
                    striped
                    hover
                    dataTable
                    columns={columns}
                    components={{
                        header: {
                            cell: cellProps => {
                                return this.customHeaderFormatters({
                                    cellProps,
                                    columns,
                                    sortingColumns,
                                    rows: sortedPaginatedRows.rows
                                });
                            }
                        }
                    }}
                >
                    <Table.Header
                        className="ds-table-header"
                        headerRows={resolve.headerRows({ columns })}
                    />
                    <Table.Body
                        rows={sortedPaginatedRows.rows}
                        rowKey={this.props.rowKey}
                        onRow={() => ({
                            role: "row"
                        })}
                    />
                </Table.PfProvider>
                <PaginationRow
                    viewType={PAGINATION_VIEW.TABLE}
                    className="ds-table-pagination"
                    pagination={pagination}
                    pageInputValue={pageChangeValue}
                    amountOfPages={sortedPaginatedRows.amountOfPages}
                    itemCount={sortedPaginatedRows.itemCount}
                    itemsStart={sortedPaginatedRows.itemsStart}
                    itemsEnd={sortedPaginatedRows.itemsEnd}
                    onPerPageSelect={this.onPerPageSelect}
                    onFirstPage={this.onFirstPage}
                    onPreviousPage={this.onPreviousPage}
                    onPageInput={this.onPageInput}
                    onNextPage={this.onNextPage}
                    onLastPage={this.onLastPage}
                    onSubmit={this.onSubmit}
                    id={this.props.searchField}
                />
            </div>
        );
    }
}

class DSShortTable extends React.Component {
    // This table is designed for smaller tables that don't need pagination or filtering,
    // and can be empty.  So log a nice "empty" value we need the columns to be dynamic
    // and relaoded every time we render
    constructor(props) {
        super(props);

        // Enables our custom header formatters extensions to reactabular
        this.customHeaderFormatters = customHeaderFormattersDefinition;
        this.currentRows = this.currentRows.bind(this);

        this.state = {
            // Sort the first column in an ascending way by default.
            sortingColumns: {
                name: {
                    direction: TABLE_SORT_DIRECTION.ASC,
                    position: 0
                }
            },
        };
    }

    currentRows(rows, columns) {
        const { sortingColumns } = this.state;
        return compose(
            sort.sorter({
                columns: columns,
                sortingColumns,
                sort: orderBy,
                strategy: sort.strategies.byProperty
            })
        )(rows);
    }

    render() {
        const sortingColumns = this.state.sortingColumns;
        const getSortingColumns = () => this.state.sortingColumns || {};
        const sortableTransform = sort.sort({
            getSortingColumns,
            onSort: selectedColumn => {
                this.setState({
                    sortingColumns: sort.byColumn({
                        sortingColumns: this.state.sortingColumns,
                        sortingOrder: defaultSortingOrder,
                        selectedColumn
                    })
                });
            },
            // Use property or index dependening on the sortingColumns structure specified
            strategy: sort.strategies.byProperty
        });

        const sortingFormatter = sort.header({
            sortableTransform,
            getSortingColumns,
            strategy: sort.strategies.byProperty
        });

        let columns = this.props.getColumns();
        for (let column of columns) {
            if (column.header.formatters.length == 0) {
                column.header.formatters = [sortingFormatter];
                column.header.transforms = [sortableTransform];
            }
        }
        const sortedRows = this.currentRows(this.props.rows, columns);

        return (
            <div>
                <Table.PfProvider
                    className="display ds-db-table"
                    striped
                    hover
                    dataTable
                    columns={columns}
                    components={{
                        header: {
                            cell: cellProps => {
                                return this.customHeaderFormatters({
                                    cellProps,
                                    columns,
                                    sortingColumns,
                                    rows: sortedRows.rows
                                });
                            }
                        }
                    }}
                >
                    <Table.Header
                        className="ds-table-header"
                        headerRows={resolve.headerRows({ columns })}
                    />
                    <Table.Body
                        rows={sortedRows}
                        rowKey={this.props.rowKey}
                        onRow={() => ({
                            role: "row"
                        })}
                    />
                </Table.PfProvider>
            </div>
        );
    }
}

// Properties

DSTable.propTypes = {
    getColumns: PropTypes.func,
    fieldsToSearch: PropTypes.array,
    rowKey: PropTypes.string,
    rows: PropTypes.array,
    toolBarSearchField: PropTypes.string,
    toolBarChildren: PropTypes.any,
    toolBarClassName: PropTypes.string,
    toolBarPlaceholder: PropTypes.string,
    toolBarLoading: PropTypes.bool,
    toolBarDisableLoadingSpinner: PropTypes.bool,
    toolBarPagination: PropTypes.array,
    toolBarPaginationPerPage: PropTypes.number,
    noSearchBar: PropTypes.bool
};

DSShortTable.propTypes = {
    getColumns: PropTypes.func,
    rowKey: PropTypes.string,
    rows: PropTypes.array
};

export { DSTable, DSShortTable };
