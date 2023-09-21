import cockpit from "cockpit";
import React from "react";
import {
    Grid,
    GridItem,
    Pagination,
    PaginationVariant,
    SearchInput,
    Spinner,
    Text,
    TextContent,
    TextVariants,
} from '@patternfly/react-core';
import {
    // cellWidth,
    expandable,
    Table,
    TableHeader,
    TableBody,
    TableVariant,
    sortable,
    SortByDirection,
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
            noRows: true,
            columns: [
                {
                    title: _("Objectclass Name"),
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                { title: _("OID"), transforms: [sortable] },
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

    handleSort(_event, index, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows pairings into a sortable array based on the column indexes
        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                expandedRow: this.state.rows[idx + 1],
                1: this.state.rows[idx].cells[0],
                2: this.state.rows[idx].cells[1],
                not_user_defined: this.state.rows[idx].disableActions
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
                    srow[1], srow[2]
                ],
                disableActions: srow.not_user_defined
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
            rows,
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
        let rows = [];
        let columns = this.state.columns;
        let count = 0;
        let noRows = false;

        for (const row of this.props.rows) {
            let user_defined = false;
            if (row.x_origin.length > 0 &&
                row.x_origin.indexOf("user defined") !== -1) {
                user_defined = true;
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [row.name[0], row.oid[0]],
                    disableActions: !user_defined,
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
                },
            );
            count += 2;
        }
        if (rows.length === 0) {
            noRows = true;
            rows = [{ cells: [_("No Objectclasses")] }];
            columns = [{ title: _("Objectclasses") }];
        }
        this.setState({
            rows,
            columns,
            noRows,
        });
    }

    handleCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            let user_defined = false;
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val !== "" && row.name[0].toLowerCase().indexOf(val) === -1 &&
                row.oid[0].toLowerCase().indexOf(val) === -1) {
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
                    cells: [row.name[0], row.oid[0]],
                    disableActions: !user_defined
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
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

    actions() {
        return [
            {
                title: _("Edit Objectclass"),
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editModalHandler(rowData.cells[0])
            },
            {
                title: _("Delete Objectclass"),
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteHandler(rowData.cells[0])
            }
        ];
    }

    render() {
        const { perPage, page, sortBy, rows, noRows, columns } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);

        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

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
                        className="ds-margin-top"
                        aria-label="oc table"
                        cells={columns}
                        rows={tableRows}
                        variant={TableVariant.compact}
                        sortBy={sortBy}
                        onSort={this.handleSort}
                        onCollapse={this.handleCollapse}
                        actions={noRows ? null : this.actions()}
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
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                { title: _("OID"), transforms: [sortable] },
                { title: _("Syntax"), transforms: [sortable] },
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

    handleSort(_event, index, direction) {
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
                not_user_defined: this.state.rows[idx].disableActions
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
                    srow[1], srow[2], srow[2]
                ],
                disableActions: srow.not_user_defined
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

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;
        let noRows = false;

        for (const row of this.props.rows) {
            let user_defined = false;
            if (row.x_origin.length > 0 &&
                row.x_origin.indexOf("user defined") !== -1) {
                user_defined = true;
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [row.name[0], row.oid[0], row.syntax[0]],
                    disableActions: !user_defined,
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
                },
            );
            count += 2;
        }
        if (rows.length === 0) {
            noRows = true;
            rows = [{ cells: [_("No Attributes")] }];
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
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            let user_defined = false;
            const val = value.toLowerCase();

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
                    cells: [row.name[0], row.oid[0], row.syntax[0]],
                    disableActions: !user_defined
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
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

    actions() {
        return [
            {
                title: _("Edit Attribute"),
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editModalHandler(rowData.cells[0])
            },
            {
                title: _("Delete Attribute"),
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteHandler(rowData.cells[0])
            }
        ];
    }

    render() {
        const { perPage, page, sortBy, rows, noRows, columns } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);

        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

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
                        className="ds-margin-top"
                        aria-label="attr table"
                        cells={columns}
                        rows={tableRows}
                        variant={TableVariant.compact}
                        sortBy={sortBy}
                        onSort={this.handleSort}
                        onCollapse={this.handleCollapse}
                        actions={noRows ? null : this.actions()}
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
                {
                    title: _("Matching Rule"),
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                { title: _("OID"), transforms: [sortable] },
                { title: _("Syntax"), transforms: [sortable] },
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

    handleSort(_event, index, direction) {
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
                    srow[1], srow[2], srow[2]
                ],
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

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;

        for (const row of this.props.rows) {
            rows.push(
                {
                    isOpen: false,
                    cells: [{ title: row.name[0] }, row.oid[0], row.syntax[0]],
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
                },
            );
            count += 2;
        }
        if (rows.length === 0) {
            rows = [{ cells: ['No Matching Rules'] }];
            columns = [{ title: 'Matching Rules' }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            const val = value.toLowerCase();
            let name = "";
            // Check for matches of all the parts
            if (row.names.length > 0) {
                name = row.name[0];
            }
            if (val !== "" && name.indexOf(val) === -1 &&
                row.oid[0].indexOf(val) === -1 &&
                row.syntax[0].indexOf(val) === -1) {
                // Not a match
                continue;
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [{ title: name === "" ? <i>&lt;{_("No Name")}&gt;</i> : name }, row.oid[0], row.syntax[0]],
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(row) }]
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
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);

        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

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
                    aria-label="attr table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.handleSort}
                    onCollapse={this.handleCollapse}
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
