import cockpit from "cockpit";
import React from 'react';
import {
	Grid,
	GridItem,
	Pagination,
	SearchInput
} from '@patternfly/react-core';
import {
	BadgeToggle,
	Dropdown,
	DropdownItem,
	DropdownPosition
} from '@patternfly/react-core/deprecated';
import {
    Table,
    Thead,
    Tbody,
    Tr,
    Th,
    Td
} from '@patternfly/react-table';

const _ = cockpit.gettext;

// TODO: Rename to GenericTable ( since it has pagination and also sorting and filtering capabilities )
class GenericPagination extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            itemCount: 0,
            page: 1,
            perPage: 10,
            pagedRows: [],
            searchValue: "",
            selectedAttrs: [],
            sortBy: {},
            isDropDownOpen: false,
            rows: [...this.props.rows],
            allRows: [...this.props.rows],
        };

        this.handleSearchChange = (event, value) => {
            let rows = [];
            const all = [];
            const val = value.toLowerCase();

            // Get fresh list of attributes and what is selected
            this.props.rows.map(attr => {
                let selected = false;
                for (const selectedAttr of this.state.selectedAttrs) {
                    if (attr.cells[0].toLowerCase() === selectedAttr.toLowerCase()) {
                        selected = true;
                        break;
                    }
                }

                all.push(
                    {
                        cells: [
                            attr.cells[0],
                            attr.cells[1],
                        ],
                        selected,
                    });
                return [];
            });

            // Process search filter on the entire list
            if (value !== "") {
                for (const row of all) {
                    const name = row.cells[0].toLowerCase();
                    const oid = row.cells[1].toLowerCase();
                    if (name.includes(val) || oid.includes(val)) {
                        rows.push(row);
                    }
                }
            } else {
                // Restore entire rows list
                rows = all;
            }

            this.setState({
                rows,
                itemCount: rows.length,
                pagedRows: rows.slice(0, this.state.perPage),
            });
        };
    }

    componentDidMount() {
        if (this.props.rows && this.props.rows.length > 0) {
            const selectedAttrs = this.props.rows
                .filter(row => row.selected)
                .map(attr => attr.cells[0]);

            this.setState({
                rows: [...this.props.rows],
                allRows: [...this.props.rows],
                selectedAttrs,
                pagedRows: this.getRowsToShow(1, this.state.perPage),
                page: 1,
                itemCount: this.props.rows.length
            });
        }
    }

    componentDidUpdate(prevProps) {
        if (this.props.tableModificationTime !== prevProps.tableModificationTime) {
            this.setState({
                rows: [...this.props.rows] || [],
                allRows: [...this.props.rows] || [],
            }, () => this.setState({
                pagedRows: this.getRowsToShow(1, this.state.perPage),
                page: 1,
                itemCount: ([...this.props.rows] || []).length
            }));
        }
    }

    onSelectRow = (event, isSelected, rowIndex) => {
        const cellValue = this.state.pagedRows[rowIndex].cells[0];
        const allItems = [...this.state.rows];
        let index = allItems.findIndex(item => item.cells[0] === cellValue);
        allItems[index].selected = isSelected;

        const allRows = [...this.state.allRows];
        index = allRows.findIndex(item => item.cells[0] === cellValue);
        allRows[index].selected = isSelected;
        const selectedAttrs = allRows.filter(row => (row.selected)).map(attr => attr.cells[0]);
        
        this.setState({
            rows: allItems,
            allRows,
            selectedAttrs,
            pagedRows: this.getRowsToShow(this.state.page, this.state.perPage)
        });
        this.props.handleSelectedAttrs(selectedAttrs);
    };

    handleDropDownToggle = isOpen => {
        this.setState({
            isDropDownOpen: isOpen
        });
    };

    handleDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isDropDownOpen: !prevState.isDropDownOpen };
        });
    };

    handleSetPage = (_event, pageNumber) => {
        this.setState({
            page: pageNumber,
            pagedRows: this.getRowsToShow(pageNumber, this.state.perPage)
        });
    };

    handlePerPageSelect = (_event, perPage) => {
        this.setState({
            page: 1,
            perPage,
            pagedRows: this.getRowsToShow(1, perPage)
        });
    };

    getRowsToShow = (page, perPage) => {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        /* if (this.props.enableSorting) {
        return this.state.sortedRows.slice(start, end);
        } */
        return this.state.rows.slice(start, end);
    };

    buildAttrDropdown = () => {
        const { isDropDownOpen, selectedAttrs } = this.state;
        const numSelected = selectedAttrs.length;
        const items = selectedAttrs.map((attr) =>
            <DropdownItem key={attr}>{attr}</DropdownItem>
        );
        let badgeProps = {
            className: "ds-badge-bgcolor"
        };
        if (numSelected === 0) {
            badgeProps = {
                isRead: true
            };
        }

        return (
            <Dropdown
                className="ds-dropdown-padding ds-margin-top-lg"
                position={DropdownPosition.left}
                onSelect={this.handleDropDownSelect}
                toggle={
                    <BadgeToggle id="toggle-attr-select" badgeProps={badgeProps} onToggle={(_event, isOpen) => this.handleDropDownToggle(isOpen)}>
                        {numSelected > 0 ? <>{numSelected} {_("selected")} </> : <>0 {_("selected")} </>}
                    </BadgeToggle>
                }
                isOpen={isDropDownOpen}
                dropdownItems={items}
            />
        );
    };

    onSort = (_event, columnIndex, sortDirection) => {
        const mySortedRows = this.state.rows
            .sort((a, b) => {
                const aValue = a.cells[columnIndex]?.title || a.cells[columnIndex];
                const bValue = b.cells[columnIndex]?.title || b.cells[columnIndex];
                return aValue < bValue ? -1 : aValue > bValue ? 1 : 0;
            });
        this.setState({
            sortBy: {
                index: columnIndex,
                direction: sortDirection
            },
            rows: sortDirection === 'asc' ? mySortedRows : mySortedRows.reverse()
        }, () => this.setState({
            pagedRows: this.getRowsToShow(this.state.page, this.state.perPage)
        }));
    };

    renderCellContent = (cell) => {
        if (cell === null || cell === undefined) {
            return '';
        }
        if (React.isValidElement(cell)) {
            return cell;
        }
        if (typeof cell === 'object') {
            if (cell.title) {
                return React.isValidElement(cell.title) 
                    ? cell.title 
                    : <span>{String(cell.title)}</span>;
            }
            return '';
        }
        // Handle string valuess(like " o=redhat")
        return String(cell).trim();
    };

    render() {
        const { itemCount, page, perPage, pagedRows } = this.state;
        const { columns = [], isSelectable = false, isSearchable = false, enableSorting = false } = this.props;
        const showPagination = itemCount > 10;

        return (
            <Grid>
                {isSelectable && this.buildAttrDropdown()}
                <GridItem span={12} className={isSelectable ? "ds-margin-top" : ""}>
                    <Grid>
                        {isSearchable && (
                            <GridItem span={5}>
                                <SearchInput
                                    className="ds-font-size-md"
                                    placeholder={_("Search")}
                                    value={this.state.searchValue}
                                    onChange={this.handleSearchChange}
                                    onClear={(evt) => this.handleSearchChange(evt, '')}
                                />
                            </GridItem>
                        )}
                        <GridItem span={isSearchable ? 7 : 12}>
                            {showPagination && (
                                <Pagination
                                    itemCount={itemCount}
                                    page={page}
                                    perPage={perPage}
                                    onSetPage={this.handleSetPage}
                                    onPerPageSelect={this.handlePerPageSelect}
                                    isCompact
                                    widgetId="pagination-options-menu-generic"
                                />
                            )}
                        </GridItem>
                    </Grid>
                </GridItem>
                <GridItem span={12}>
                    <Table aria-label={this.props.ariaLabel || "generic table"} variant="compact">
                        <Thead>
                            <Tr>{isSelectable && <Th/>}{columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={enableSorting ? {
                                        sortBy: this.state.sortBy,
                                        onSort: (_evt, index, direction) => this.onSort(_evt, columnIndex, direction),
                                        columnIndex
                                    } : undefined}
                                >
                                    {typeof column === 'object' ? column.title : column}
                                </Th>
                            ))}</Tr>
                        </Thead>
                        <Tbody>
                            {(pagedRows || []).map((row, rowIndex) => {
                                const rowCells = Array.isArray(row) ? row : row.cells || [];
                                return (
                                    <Tr key={rowIndex}>
                                        {isSelectable && (
                                            <Td
                                                select={{
                                                    rowIndex,
                                                    onSelect: this.onSelectRow,
                                                    isSelected: row.selected
                                                }}
                                            />
                                        )}
                                        {rowCells.map((cell, cellIndex) => (
                                            <Td
                                                key={`${rowIndex}_${cellIndex}`}
                                                dataLabel={columns[cellIndex]?.title || columns[cellIndex]}
                                            >
                                                {this.renderCellContent(cell)}
                                            </Td>
                                        ))}
                                    </Tr>
                                );
                            })}
                        </Tbody>
                    </Table>
                </GridItem>
            </Grid>
        );
    }
}

GenericPagination.defaultProps = {
    rows: [],
    columns: [],
    isSelectable: false,
    isSearchable: false,
    enableSorting: false,
    handleSelectedAttrs: () => {},
    ariaLabel: "generic table"
};

export default GenericPagination;
