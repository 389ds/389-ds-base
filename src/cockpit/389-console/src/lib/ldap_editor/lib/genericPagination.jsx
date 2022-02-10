import React from 'react';
import {
    BadgeToggle,
    Grid,
    GridItem,
    Dropdown, DropdownItem, DropdownPosition, DropdownToggle, DropdownToggleCheckbox,
    Pagination,
    SearchInput,
    Toolbar, ToolbarGroup, ToolbarItem
} from '@patternfly/react-core';
import {
    Table, TableHeader, TableBody, TableVariant,
    SortByDirection
} from '@patternfly/react-table';

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
            // sortedRows: this.props.enableSorting ? [...this.props.rows] : null,
            sortBy: {},
            isDropDownOpen: false,
            rows: [...this.props.rows],
            allRows: [...this.props.rows],
        };

        this.onSort = (_event, index, direction) => {
            const mySortedRows = this.state.rows
                .sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
            this.setState({
                sortBy: {
                    index,
                    direction
                },
                rows: direction === SortByDirection.asc
                    ? mySortedRows
                    : mySortedRows.reverse()
            }, () => this.setState({ // Need to update this.state.rows prior to run getRowsToShow().
                pagedRows: this.getRowsToShow(this.state.page, this.state.perPage)
            }));
        }

        this.onSearchChange = (value, event) => {
            let rows = [];
            let all = [];
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
                        selected: selected,
                    });
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
                rows: rows,
                itemCount: rows.length,
                pagedRows: rows.slice(0, this.state.perPage),
            })
        }
    }

    componentDidMount () {
        const selectedAttrs = this.props.rows.filter(row => (row.selected)).map(attr => attr.cells[0]);
        this.setState({
            rows: [...this.props.rows],
            allRows: [...this.props.rows],
            selectedAttrs
        }, () => this.setState({
            // pagedRows: this.getRowsToShow(this.state.page, this.state.perPage),
            pagedRows: this.getRowsToShow(1, this.state.perPage),
            page: 1,
            itemCount: this.props.rows.length
        }));
    }

    componentDidUpdate (prevProps) {
        if (this.props.tableModificationTime !== prevProps.tableModificationTime) {
            this.setState({
                rows: [...this.props.rows],
                allRows: [...this.props.rows],
            }, () => this.setState({
                // pagedRows: this.getRowsToShow(this.state.page, this.state.perPage),
                pagedRows: this.getRowsToShow(1, this.state.perPage),
                page: 1,
                itemCount: this.props.rows.length
            }));
        }
    }

    onSelectRow = (_event, isSelected, rowId) => {
        // const rows = [...this.state.pagedRows];
        // rows[rowId].selected = isSelected;

        // Find the row in the full array and set 'selected' property accordingly
        // The rowId cannot be used since it changes with the pagination.
        // TODO: Need to make sure the column used has unique values for each row.
        // TODO: Pass the colum to use as a property.
        const cellValue = this.state.pagedRows[rowId].cells[0];
        const allItems = [...this.state.rows];
        let index = allItems.findIndex(item => item.cells[0] === cellValue);
        allItems[index].selected = isSelected;

        // Update all rows so our selected attributes stay accurate as "search" could mess rows
        index = this.state.allRows.findIndex(item => item.cells[0] === cellValue);
        this.state.allRows[index].selected = isSelected;
        const selectedAttrs = this.state.allRows.filter(row => (row.selected)).map(attr => attr.cells[0]);

        this.setState({
            rows: allItems,
            selectedAttrs,
            pagedRows: this.getRowsToShow(this.state.page, this.state.perPage)
        });
        this.props.handleSelectedAttrs(selectedAttrs);
    };

    onDropDownToggle = isOpen => {
        this.setState({
            isDropDownOpen: isOpen
        });
    };

    onDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isDropDownOpen: !prevState.isDropDownOpen };
        });
    };

    onSetPage = (_event, pageNumber) => {
        this.setState({
            page: pageNumber,
            pagedRows: this.getRowsToShow(pageNumber, this.state.perPage)
        });
    };

    onPerPageSelect = (_event, perPage) => {
        this.setState({
            page: 1,
            perPage: perPage,
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
    }

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
                onSelect={this.onDropDownSelect}
                toggle={
                    <BadgeToggle id="toggle-attr-select" badgeProps={badgeProps} onToggle={this.onDropDownToggle}>
                        {numSelected > 0 ? <>{numSelected} selected </> : <>0 selected </>}
                    </BadgeToggle>
                }
                isOpen={isDropDownOpen}
                dropdownItems={items}
            />
        );
    }

    render () {
        const {
            itemCount, page, perPage, pagedRows,
            isDropDownOpen
        } = this.state;

        // Enable pagination if the number of rows is higher than 10.
        const showPagination = itemCount > 10;

        return (
            <Grid>
                {this.props.isSelectable &&
                    this.buildAttrDropdown()
                }
                <GridItem span={12} className={this.props.isSelectable ? "ds-margin-top" : ""}>
                    <Grid>
                        { this.props.isSearchable &&
                            <GridItem span={5}>
                                <SearchInput
                                    className="ds-font-size-md"
                                    placeholder='Search'
                                    value={this.state.searchValue}
                                    onChange={this.onSearchChange}
                                    onClear={(evt) => this.onSearchChange('', evt)}
                                />
                            </GridItem>
                        }
                        <GridItem span={this.props.isSearchable ? 7 : 12}>
                            { showPagination &&
                                <Pagination
                                    itemCount={itemCount}
                                    page={page}
                                    perPage={perPage}
                                    onSetPage={this.onSetPage}
                                    onPerPageSelect={this.onPerPageSelect}
                                    isCompact
                                    widgetId="pagination-options-menu-generic"
                                />
                            }
                        </GridItem>
                    </Grid>
                </GridItem>
                <GridItem span={12}>
                    <Table
                        variant={TableVariant.compact}
                        rows={pagedRows}
                        cells={this.props.columns}
                        actions={this.props.actions}
                        aria-label="generic table"
                        caption={this.props.caption}
                        sortBy={this.props.enableSorting ? this.state.sortBy : null}
                        onSort={this.props.enableSorting ? this.onSort : null}
                        canSelectAll={this.props.canSelectAll}
                        onSelect={this.props.isSelectable ? this.onSelectRow : null}
                    >
                        <TableHeader />
                        <TableBody />
                    </Table>
                </GridItem>
            </Grid>
        );
    }
}

export default GenericPagination;
