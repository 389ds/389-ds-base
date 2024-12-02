import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Divider,
    Pagination,
    PaginationVariant,
    SearchInput,
} from '@patternfly/react-core';
import {
    Table,
    Thead,
    Tbody,
    Tr,
    Th,
    Td
} from '@patternfly/react-table';

const _ = cockpit.gettext;

class GroupTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("Member DN"), sort: 'asc' },
            ],
            pagedRows: [],
        };
        this.selectedCount = 0;

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber,
                pagedRows: this.getRowsToShow(pageNumber, this.state.perPage)
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1,
                pagedRows: this.getRowsToShow(1, perPage)
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
    }

    componentDidMount() {
        let rows = [];
        const columns = this.state.columns;

        for (const attrRow of this.props.rows) {
            let selected = false;
            if (this.props.delMemberList.includes(attrRow)) {
                selected = true;
            }
            rows.push({
                cells: [attrRow],
                selected,
            });
        }
        if (rows.length === 0) {
            rows = [{ cells: [_("No Members")], disableSelection: true }];
        }
        this.setState({
            rows,
            columns,
            pagedRows: this.getRowsToShow(1, this.state.perPage)
        });
    }

    getRowsToShow = (page, perPage) => {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        return this.state.rows.slice(start, end);
    };

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        for (const row of this.props.rows) {
            if (val !== "" && val !== "*" && row.indexOf(val) === -1) {
                continue;
            }
            rows.push({
                cells: [row]
            });
        }
        if (rows.length === 0) {
            rows = [{ cells: [_("No Members")], disableSelection: true }];
        }

        this.setState({
            rows,
            value,
            page: 1,
            pagedRows: this.getRowsToShow(1, this.state.perPage)
        });
    }

    handleSort(_event, columnIndex, sortDirection) {
        const rows = [];
        const sortedAttrs = [...this.props.rows];

        sortedAttrs.sort();
        if (sortDirection !== 'asc') {
            sortedAttrs.reverse();
        }
        for (const attrRow of sortedAttrs) {
            rows.push({ cells: [attrRow] });
        }

        this.setState({
            sortBy: {
                index: columnIndex,
                direction: sortDirection
            },
            rows,
            page: 1,
            pagedRows: this.getRowsToShow(1, this.state.perPage)
        });
    }

    getActionsForRow = (rowData) => {
        // Return empty array if it's the "No Members" row
        if (rowData.cells.length === 1 && rowData.cells[0] === _("No Members")) {
            return [];
        }
        
        // Only return actions for valid rows
        if (!rowData.cells) {
            return [];
        }
        
        return [
            {
                title: _("View Entry"),
                onClick: () => this.props.viewEntry(rowData.cells[0])
            },
            {
                title: _("Edit Entry"),
                onClick: () => this.props.editEntry(rowData.cells[0])
            },
            {
                isSeparator: true
            },
            {
                title: _("Remove Entry"),
                onClick: () => this.props.removeMember(rowData.cells[0])
            }
        ];
    };

    render() {
        const { columns, pagedRows, perPage, page, sortBy } = this.state;
        const extraPrimaryProps = {};
        let deleteBtnName = _("Remove Selected Members");
        if (this.props.saving) {
            deleteBtnName = _("Updating group ...");
            extraPrimaryProps.spinnerAriaValueText = _("Updating");
        }

        return (
            <div className="ds-margin-top-xlg ds-indent">
                <SearchInput
                    className="ds-margin-top"
                    placeholder={_("Search Members")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table aria-label="group table" variant="compact">
                    <Thead>
                        <Tr>
                            <Th
                                sort={{
                                    sortBy,
                                    onSort: this.handleSort,
                                    columnIndex: 0
                                }}
                            >
                                {columns[0].title}
                            </Th>
                        </Tr>
                    </Thead>
                    <Tbody>
                        {pagedRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                <Td
                                    select={{
                                        rowIndex,
                                        onSelect: (_event, isSelecting) => {
                                            this.props.onSelectMember(row.cells[0], isSelecting);
                                        },
                                        isSelected: row.selected,
                                        disable: row.disableSelection
                                    }}
                                    actions={this.getActionsForRow(row)}
                                >
                                    {row.cells[0]}
                                </Td>
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
                />
                <Button
                    isDisabled={this.props.delMemberList.length === 0 || this.props.saving}
                    variant="primary"
                    onClick={this.props.handleShowConfirmBulkDelete}
                    isLoading={this.props.saving}
                    {...extraPrimaryProps}
                >
                    {deleteBtnName}
                </Button>
                <Divider className="ds-margin-top-lg" />
            </div>
        );
    }
}

export default GroupTable;
