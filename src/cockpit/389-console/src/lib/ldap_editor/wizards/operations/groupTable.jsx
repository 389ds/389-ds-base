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
    TableHeader,
    TableBody,
    TableVariant,
    sortable,
    SortByDirection,
} from '@patternfly/react-table';

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
                { title: 'Member DN', transforms: [sortable] },
            ],
        };
        this.selectedCount = 0;

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
            rows = [{ cells: ['No Members'], disableSelection: true }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        for (const row of this.props.rows) {
            if (val !== "" && val !== "*" && row.indexOf(val) === -1) {
                // Not a match, skip it
                continue;
            }
            rows.push({
                cells: [row]
            });
        }
        if (rows.length === 0) {
            rows = [{ cells: ['No Members'], disableSelection: true }];
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    handleSort(_event, index, direction) {
        const rows = [];
        const sortedAttrs = [...this.props.rows];

        // Sort the referrals and build the new rows
        sortedAttrs.sort();
        if (direction !== SortByDirection.asc) {
            sortedAttrs.reverse();
        }
        for (const attrRow of sortedAttrs) {
            rows.push({ cells: [attrRow] });
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

    actions() {
        return [
            {
                title: 'View Entry',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.viewEntry(rowData.cells[0])
            },
            {
                title: 'Edit Entry',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editEntry(rowData.cells[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Remove Entry',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.removeMember(rowData.cells[0])
            }
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const extraPrimaryProps = {};
        const canSelectAll = false;
        let deleteBtnName = "Remove Selected Members";
        if (this.props.saving) {
            deleteBtnName = "Updating group ...";
            extraPrimaryProps.spinnerAriaValueText = "Updating";
        }

        return (
            <div className="ds-margin-top-xlg ds-indent">
                <SearchInput
                    className="ds-margin-top"
                    placeholder='Search Members'
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table
                    className="ds-margin-top"
                    canSelectAll={canSelectAll}
                    onSelect={(_event, isSelecting, rowIndex) => {
                        if (rowIndex !== -1) {
                            this.props.onSelectMember(this.state.rows[rowIndex].cells[0], isSelecting);
                        }
                    }}
                    aria-label="group table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.handleSort}
                    actions={rows.length > 0 ? this.actions() : null}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
                />
                <Button
                    isDisabled={this.props.delMemberList.length === 0 || this.props.saving}
                    variant="primary"
                    onClick={this.props.handleShowConfirmBulkDelete}
                    isLoading={this.props.saving}
                    spinnerAriaValueText={this.state.saving ? "Updating group ..." : undefined}
                    {...extraPrimaryProps}
                >
                    {deleteBtnName}
                </Button>
                <Divider
                    className="ds-margin-top-lg"
                />
            </div>
        );
    }
}

export default GroupTable;
