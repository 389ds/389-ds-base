import React from "react";
import {
    PaginationRow,
    paginate,
    Table,
    noop,
    actionHeaderCellFormatter,
    customHeaderFormattersDefinition,
    defaultSortingOrder,
    sortableHeaderCellFormatter,
    tableCellFormatter,
    TABLE_SORT_DIRECTION,
    PAGINATION_VIEW
} from "patternfly-react";
import PropTypes from "prop-types";
import { orderBy } from "lodash";
import * as sort from "sortabular";
import * as resolve from "table-resolver";
import { compose } from "recompose";
import { searchFilter } from "../tools.jsx";
import CustomTableToolbar from "../customTableToolbar.jsx";
import "../../css/ds.css";

class PluginTable extends React.Component {
    constructor(props) {
        super(props);

        // Point the transform to your sortingColumns. React state can work for this purpose
        // but you can use a state manager as well.
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

        // Enables our custom header formatters extensions to reactabular
        this.customHeaderFormatters = customHeaderFormattersDefinition;

        this.handleSearchValueChange = this.handleSearchValueChange.bind(this);
        this.hideDropdown = this.hideDropdown.bind(this);
        this.toggleDropdownShown = this.toggleDropdownShown.bind(this);
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
            dropdownShown: false,
            searchFilterValue: "",
            fieldsToSearch: ["cn", "nsslapd-pluginType"],
            // Sort the first column in an ascending way by default.
            sortingColumns: {
                name: {
                    direction: TABLE_SORT_DIRECTION.ASC,
                    position: 0
                }
            },
            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Plugin Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [sortableTransform],
                        formatters: [sortingFormatter],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 0
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "nsslapd-pluginType",
                    header: {
                        label: "Plugin Type",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [sortableTransform],
                        formatters: [sortingFormatter],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 1
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "nsslapd-pluginEnabled",
                    header: {
                        label: "Enabled",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [sortableTransform],
                        formatters: [sortingFormatter],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 2
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        label: "Actions",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <Table.Actions key="0">
                                        <Table.Button
                                            onClick={() => {
                                                this.props.loadModalHandler(
                                                    rowData
                                                );
                                            }}
                                        >
                                            Edit Plugin
                                        </Table.Button>
                                    </Table.Actions>
                                ];
                            }
                        ]
                    }
                }
            ],

            // pagination default states
            pagination: {
                page: 1,
                perPage: 12,
                perPageOptions: [6, 12, 24]
            },

            // page input value
            pageChangeValue: 1
        };
    }

    handleSearchValueChange(event) {
        this.setState({ searchFilterValue: event.target.value });
    }

    hideDropdown() {
        const { onExit } = this.props;
        this.setState({ dropdownShown: false });
        onExit();
    }

    toggleDropdownShown() {
        this.setState(prevState => ({
            dropdownShown: !prevState.dropdownShown
        }));
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

        return (
            <div>
                <CustomTableToolbar
                    modelToSearch="Plugins"
                    searchFilterValue={this.state.searchFilterValue}
                    handleValueChange={this.handleSearchValueChange}
                    loading={this.props.loading}
                />
                <Table.PfProvider
                    className="display ds-repl-table"
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
                        rowKey="cn"
                        onRow={this.onRow}
                    />
                </Table.PfProvider>
                <PaginationRow
                    viewType={PAGINATION_VIEW.TABLE}
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
                />
            </div>
        );
    }
}

PluginTable.propTypes = {
    rows: PropTypes.array,
    loadModalHandler: PropTypes.func,
    loading: PropTypes.bool
};

PluginTable.defaultProps = {
    rows: [],
    loadModalHandler: noop,
    loading: false
};

export default PluginTable;
