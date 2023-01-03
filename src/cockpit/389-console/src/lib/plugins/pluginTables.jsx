import React from "react";
import {
    Grid,
    GridItem,
    Pagination,
    PaginationVariant,
    SearchInput,
    Switch,
} from "@patternfly/react-core";
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

class PluginTable extends React.Component {
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
                    title: 'Plugin Name',
                    transforms: [sortable],
                    cellFormatters: [expandable]
                },
                { title: 'Plugin Type', transforms: [sortable] },
                { title: 'Enabled', transforms: [sortable] },
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

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
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

    onSearchChange(value, event) {
        const rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val != "" && row.cn[0].toLowerCase().indexOf(val) == -1 &&
                row["nsslapd-pluginType"][0].toLowerCase().indexOf(val) == -1) {
                // Not a match
                continue;
            }

            rows.push(
                {
                    isOpen: false,
                    cells: [
                        row.cn[0],
                        row["nsslapd-pluginType"][0],
                        row["nsslapd-pluginEnabled"][0]
                    ],
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
            rows: rows,
            value: value,
            page: 1,
        });
    }

    getExpandedRow(rowData) {
        const dependsType = rowData["nsslapd-plugin-depends-on-type"] === undefined
            ? ""
            : rowData["nsslapd-plugin-depends-on-type"].join(", ");
        const dependsNamed = rowData["nsslapd-plugin-depends-on-named"] === undefined
            ? ""
            : rowData["nsslapd-plugin-depends-on-named"].join(", ");
        const precedence = rowData["nsslapd-pluginprecedence"] === undefined
            ? ""
            : rowData["nsslapd-pluginprecedence"][0];

        const plugin_enabled = rowData["nsslapd-pluginEnabled"][0] == "on";
        // const plugin_name = (' ' + rowData["cn"][0]).slice(1);
        const plugin_name = rowData.cn[0];
        const enabled = <i>Plugin is enabled</i>;
        const disabled = <i>Plugin is disabled</i>;

        return (
            <Grid className="ds-left-indent-xlg">
                <GridItem span={4}><b>Plugin Description:</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginDescription"][0]}</i></GridItem>
                <GridItem span={4}><b>Plugin Path:</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginPath"][0]}</i></GridItem>
                <GridItem span={4}><b>Plugin Init Function:</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginInitfunc"][0]}</i></GridItem>
                <GridItem span={4}><b>Plugin ID:</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginId"][0]}</i></GridItem>
                <GridItem span={4}><b>Plugin Vendor:</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginVendor"][0]}</i></GridItem>
                <GridItem span={4}><b>Plugin Version:</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginVersion"][0]}</i></GridItem>
                <GridItem span={4}><b>Plugin Depends On Named:</b></GridItem>
                <GridItem span={8}><i>{dependsNamed}</i></GridItem>
                <GridItem span={4}><b>Plugin Depends On Type:</b></GridItem>
                <GridItem span={8}><i>{dependsType}</i></GridItem>
                <GridItem span={4}><b>Plugin Precedence:</b></GridItem>
                <GridItem span={8}><i>{precedence}</i></GridItem>
                <GridItem span={12} className="ds-margin-top-lg">
                    <Switch
                        id={plugin_name}
                        key={plugin_name}
                        label={enabled}
                        labelOff={disabled}
                        isChecked={plugin_enabled}
                        onChange={() => { this.props.showConfirmToggle(plugin_name, plugin_enabled) }}
                    />
                </GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        const rows = [];
        let count = 0;

        for (const row of this.props.rows) {
            rows.push(
                {
                    isOpen: false,
                    cells: [
                        row.cn[0],
                        row["nsslapd-pluginType"][0],
                        row["nsslapd-pluginEnabled"][0]
                    ],
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
            rows: rows,
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
            <div className={this.state.toggleSpinning ? "ds-disabled" : ""}>
                <SearchInput
                    placeholder='Search Plugins'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="all plugins table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    onCollapse={this.onCollapse}
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

PluginTable.propTypes = {
    rows: PropTypes.array,
};

PluginTable.defaultProps = {
    rows: [],
};

class AttrUniqConfigTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Config Name', transforms: [sortable] },
                { title: 'Attribute', transforms: [sortable] },
                { title: 'Enabled', transforms: [sortable] }
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    actions() {
        return [
            {
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    componentDidMount () {
        // Copy the rows so we can handle sorting and searching
        const rows = [];
        for (const row of this.props.rows) {
            rows.push([row.cn[0], row['uniqueness-attribute-name'].join(", "), row["nsslapd-pluginenabled"][0]]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                rows.push([row.cn[0], row['uniqueness-attribute-name'].join(", "), row["nsslapd-pluginenabled"][0]]);
            }
        }
        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Attribute Uniqueness Configurations' }];
            tableRows = [{ cells: ['No Configurations'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    placeholder='Search Configurations'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="glue table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

AttrUniqConfigTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

AttrUniqConfigTable.defaultProps = {
    rows: [],
};

class LinkedAttributesTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Config Name', transforms: [sortable] },
                { title: 'Link Type', transforms: [sortable] },
                { title: 'Managed Type', transforms: [sortable] },
                { title: 'Link Scope', transforms: [sortable] }
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    actions() {
        return [
            {
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    componentDidMount () {
        // Copy the rows so we can handle sorting and searching
        const rows = [];
        for (const row of this.props.rows) {
            const configName = row.cn === undefined ? "" : row.cn[0];
            const linkType = row.linktype === undefined ? "" : row.linktype[0];
            const managedType = row.managedtype === undefined ? "" : row.managedtype[0];
            const linkScope = row.linkscope === undefined ? "" : row.linkscope[0];
            rows.push([configName, linkType, managedType, linkScope]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1 &&
                row[3].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                const configName = row.cn === undefined ? "" : row.cn[0];
                const linkType = row.linktype === undefined ? "" : row.linktype[0];
                const managedType = row.managedtype === undefined ? "" : row.managedtype[0];
                const linkScope = row.linkscope === undefined ? "" : row.linkscope[0];
                rows.push([configName, linkType, managedType, linkScope]);
            }
        }
        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Linked Attributes Configurations' }];
            tableRows = [{ cells: ['No Configurations'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    placeholder='Search Configurations'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="linked table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

LinkedAttributesTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

LinkedAttributesTable.defaultProps = {
    rows: [],
};

class DNATable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Config Name', transforms: [sortable] },
                { title: 'Scope', transforms: [sortable] },
                { title: 'Filter', transforms: [sortable] },
                { title: 'Next Value', transforms: [sortable] }
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        // Copy the rows so we can handle sorting and searching
        const rows = [];
        for (const row of this.props.rows) {
            const configName = row.cn === undefined ? "" : row.cn[0];
            const nextValue = row.dnanextvalue === undefined ? "" : row.dnanextvalue[0];
            const filter = row.dnafilter === undefined ? "" : row.dnafilter[0];
            const scope = row.dnascope === undefined ? "" : row.dnascope[0];
            rows.push([configName, scope, filter, nextValue]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1 &&
                row[3].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                const configName = row.cn === undefined ? "" : row.cn[0];
                const nextValue = row.dnanextvalue === undefined ? "" : row.dnanextvalue[0];
                const filter = row.dnafilter === undefined ? "" : row.dnafilter[0];
                const scope = row.scope === undefined ? "" : row.scope[0];
                rows.push([configName, scope, filter, nextValue]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'DNA Configurations' }];
            tableRows = [{ cells: ['No Configurations'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder='Search Configurations'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="dna table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

DNATable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

DNATable.defaultProps = {
    rows: [],
};

class DNASharedTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Hostname', transforms: [sortable] },
                { title: 'Port', transforms: [sortable] },
                { title: 'Remaining Values', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            rows.push([
                row.dnahostname[0],
                row.dnaportnum[0],
                row.dnaremainingvalues[0]
            ]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                rows.push([
                    row.dnahostname[0],
                    row.dnaportnum[0],
                    row.dnaremainingvalues[0]
                ]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0] + ":" + rowData[1])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0] + ":" + rowData[1])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'DNA Shared Configurations' }];
            tableRows = [{ cells: ['No Configurations'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder='Search Shared Configurations'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="dna shared table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

DNASharedTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

DNASharedTable.defaultProps = {
    rows: [],
};

class AutoMembershipDefinitionTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Definition Name', transforms: [sortable] },
                { title: 'Default Group', transforms: [sortable] },
                { title: 'Scope', transforms: [sortable] },
                { title: 'Filter', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            rows.push([
                row.cn[0],
                "automemberdefaultgroup" in row ? row.automemberdefaultgroup[0] : "",
                row.automemberscope[0],
                row.automemberfilter[0],
            ]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1 &&
                row[3].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                rows.push([
                    row.cn[0],
                    row.automemberdefaultgroup[0],
                    row.automemberscope[0],
                    row.automemberfilter[0],
                ]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Automembership Definitions' }];
            tableRows = [{ cells: ['No Definitions'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    className="ds-margin-top-lg"
                    placeholder='Search Definitions'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="automember def table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

AutoMembershipDefinitionTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

AutoMembershipDefinitionTable.defaultProps = {
    rows: [],
};

class AutoMembershipRegexTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Config Name', transforms: [sortable] },
                { title: 'Exclusive Regex', transforms: [sortable] },
                { title: 'Inclusive Regex', transforms: [sortable] },
                { title: 'Target Group', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            const includeReg = row.automemberinclusiveregex === undefined ? "" : row.automemberinclusiveregex.join(", ");
            const excludeReg = row.automemberexclusiveregex === undefined ? "" : row.automemberexclusiveregex.join(", ");
            const targetGrp = row.automembertargetgroup === undefined ? "" : row.automembertargetgroup[0];
            rows.push([row.cn[0], excludeReg, includeReg, targetGrp]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1 &&
                row[3].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                const includeReg = row.automemberinclusiveregex === undefined ? "" : row.automemberinclusiveregex[0];
                const excludeReg = row.automemberexclusiveregex === undefined ? "" : row.automemberexclusiveregex[0];
                const targetGrp = row.automembertargetgroup === undefined ? "" : row.automembertargetgroup[0];
                rows.push([
                    row.cn[0],
                    excludeReg.join(", "),
                    includeReg.join(", "),
                    targetGrp,
                ]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Automembership Regular Expressions' }];
            tableRows = [{ cells: ['No regular expressions'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    className="ds-margin-top-lg"
                    placeholder='Search Regular Expressions'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="automember regex table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

AutoMembershipRegexTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

AutoMembershipRegexTable.defaultProps = {
    rows: [],
};

class ManagedDefinitionTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Config Name', transforms: [sortable] },
                { title: 'Scope', transforms: [sortable] },
                { title: 'Filter', transforms: [sortable] },
                { title: 'Managed Base', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            const managedBase = row.managedbase === undefined ? "" : row.managedbase[0];
            const scope = row.originscope === undefined ? "" : row.originscope[0];
            const filter = row.originfilter === undefined ? "" : row.originfilter[0];
            rows.push([row.cn[0], scope, filter, managedBase]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1 &&
                row[3].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                const managedBase = row.managedbase === undefined ? "" : row.managedbase[0];
                const scope = row.originscope === undefined ? "" : row.originscope[0];
                const filter = row.originfilter === undefined ? "" : row.originfilter[0];
                rows.push([row.cn[0], scope, filter, managedBase]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Managed Entry Definitions' }];
            tableRows = [{ cells: ['No definitions'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    className="ds-margin-top-lg"
                    placeholder='Search Definitions'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="managed def table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

ManagedDefinitionTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

ManagedDefinitionTable.defaultProps = {
    rows: [],
};

class ManagedTemplateTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Template DN', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            rows.push([row.entrydn[0]]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" && row[0].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                rows.push([row.entrydn[0]]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Managed Entry Templates' }];
            tableRows = [{ cells: ['No templates'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    placeholder='Search Templates'
                    className="ds-margin-top-lg"
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="managed template table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

ManagedTemplateTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

ManagedTemplateTable.defaultProps = {
    rows: [],
};

class PassthroughAuthURLsTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'URL', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            rows.push([row.url]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" && row[0].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                rows.push([row.url]);
            }
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
                title: 'Edit URL',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete URL',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'Pass-Through Authentication URLs' }];
            tableRows = [{ cells: ['No URLs'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    placeholder='Search'
                    className="ds-margin-top-lg"
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="passthru url table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

PassthroughAuthURLsTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

PassthroughAuthURLsTable.defaultProps = {
    rows: [],
};

class PassthroughAuthConfigsTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Config Name', transforms: [sortable] },
                { title: 'Attribute', transforms: [sortable] },
                { title: 'Map Method', transforms: [sortable] },
                { title: 'Filter', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        const rows = [];
        for (const row of this.props.rows) {
            const attr = row.pamidattr === undefined ? "" : row.pamidattr[0];
            const mapMethod = row.pamidmapmethod === undefined ? "" : row.pamidmapmethod[0];
            const filter = row.pamfilter === undefined ? "" : row.pamfilter[0];
            rows.push([row.cn[0], attr, mapMethod, filter]);
        }
        this.setState({
            rows: rows
        });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1 &&
                row[3].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3]]);
        }
        if (val == "") {
            // reset rows
            for (const row of this.props.rows) {
                const attr = row.pamidattr === undefined ? "" : row.pamidattr[0];
                const mapMethod = row.pamidmapmethod === undefined ? "" : row.pamidmapmethod[0];
                const filter = row.pamfilter === undefined ? "" : row.pamfilter[0];
                rows.push([row.cn[0], attr, mapMethod, filter]);
            }
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
                title: 'Edit Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0])
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete Config',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;
        if (rows.length == 0) {
            has_rows = false;
            columns = [{ title: 'PAM Configurations' }];
            tableRows = [{ cells: ['No PAM configurations'] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }
        return (
            <div>
                <SearchInput
                    placeholder='Search'
                    className="ds-margin-top-lg"
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="pass config table"
                    cells={columns}
                    rows={tableRows}
                    variant={TableVariant.compact}
                    sortBy={this.state.sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

PassthroughAuthConfigsTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

PassthroughAuthConfigsTable.defaultProps = {
    rows: [],
};

export {
    PluginTable,
    AttrUniqConfigTable,
    LinkedAttributesTable,
    DNATable,
    DNASharedTable,
    AutoMembershipDefinitionTable,
    AutoMembershipRegexTable,
    ManagedDefinitionTable,
    ManagedTemplateTable,
    PassthroughAuthURLsTable,
    PassthroughAuthConfigsTable
};
