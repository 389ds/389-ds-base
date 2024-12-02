import cockpit from "cockpit";
import React from "react";
import {
    Grid,
    GridItem,
    Pagination,
    SearchInput,
    Switch,
} from "@patternfly/react-core";
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

class PluginTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("Plugin Name"), sortable: true },
                { title: _("Plugin Type"), sortable: true },
                { title: _("Enabled"), sortable: true },
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

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        const index = (this.state.perPage * (this.state.page - 1)) + rowIndex;
        rows[index].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSort(_event, columnIndex, direction) {
        const rows = [...this.state.rows];
        
        rows.sort((a, b) => (a.cells[columnIndex].content > b.cells[columnIndex].content) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            rows.reverse();
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

    handleSearchChange(event, value) {
        const rows = [];

        for (const row of this.props.rows) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val !== "" && row.cn[0].toLowerCase().indexOf(val) === -1 &&
                row["nsslapd-pluginType"][0].toLowerCase().indexOf(val) === -1) {
                // Not a match
                continue;
            }

            rows.push({
                isOpen: false,
                cells: [
                    { content: row.cn[0] },
                    { content: row["nsslapd-pluginType"][0] },
                    { content: row["nsslapd-pluginEnabled"][0] }
                ],
                originalData: row
            });
        }

        this.setState({
            rows,
            value,
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

        const plugin_enabled = rowData["nsslapd-pluginEnabled"][0] === "on";
        // const plugin_name = (' ' + rowData["cn"][0]).slice(1);
        const plugin_name = rowData.cn[0];
        const enabled = <i>{_("Plugin is enabled")}</i>;
        const disabled = <i>{_("Plugin is disabled")}</i>;

        return (
            <Grid className="ds-left-indent-xlg">
                <GridItem span={4}><b>{_("Plugin Description:")}</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginDescription"][0]}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Path:")}</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginPath"][0]}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Init Function:")}</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginInitfunc"][0]}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin ID:")}</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginId"][0]}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Vendor:")}</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginVendor"][0]}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Version:")}</b></GridItem>
                <GridItem span={8}><i>{rowData["nsslapd-pluginVersion"][0]}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Depends On Named:")}</b></GridItem>
                <GridItem span={8}><i>{dependsNamed}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Depends On Type:")}</b></GridItem>
                <GridItem span={8}><i>{dependsType}</i></GridItem>
                <GridItem span={4}><b>{_("Plugin Precedence:")}</b></GridItem>
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

        for (const row of this.props.rows) {
            // Create row with properly formatted cells
            rows.push({
                isOpen: false,
                cells: [
                    { content: row.cn[0] },
                    { content: row["nsslapd-pluginType"][0] },
                    { content: row["nsslapd-pluginEnabled"][0] }
                ],
                originalData: row
            });
        }
        this.setState({
            rows,
        });
    }

    render() {
        const { perPage, page, sortBy, rows, columns } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);

        return (
            <div className={this.state.toggleSpinning ? "ds-disabled" : ""}>
                <SearchInput
                    placeholder={_("Search Plugins")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table
                    aria-label="all plugins table"
                    variant="compact"
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
                                </Tr>
                                {row.isOpen && (
                                    <Tr isExpanded={true}>
                                        <Td colSpan={columns.length + 1}>
                                            <ExpandableRowContent>
                                                {this.getExpandedRow(row.originalData)}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Config Name"), sortable: true },
                { title: _("Attribute"), sortable: true },
                { title: _("Enabled"), sortable: true }
            ],
        };

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

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    componentDidMount() {
        const rows = this.props.rows.map(row => [
            row.cn[0],
            row['uniqueness-attribute-name'].join(", "),
            row["nsslapd-pluginenabled"][0]
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn[0],
                row['uniqueness-attribute-name'].join(", "),
                row["nsslapd-pluginenabled"][0]
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row[0].toLowerCase().includes(val) ||
                row[1].toLowerCase().includes(val)
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Attribute Uniqueness Configurations") }];
            tableRows = [{ cells: [_("No Configurations")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search Configurations")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="attribute uniqueness config table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Config Name"), sortable: true },
                { title: _("Link Type"), sortable: true },
                { title: _("Managed Type"), sortable: true },
                { title: _("Link Scope"), sortable: true }
            ],
        };

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

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    componentDidMount() {
        const rows = this.props.rows.map(row => [
            row.cn?.[0] || "",
            row.linktype?.[0] || "",
            row.managedtype?.[0] || "",
            row.linkscope?.[0] || ""
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn?.[0] || "",
                row.linktype?.[0] || "",
                row.managedtype?.[0] || "",
                row.linkscope?.[0] || ""
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Linked Attributes Configurations") }];
            tableRows = [{ cells: [_("No Configurations")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search Configurations")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="linked attributes table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Config Name"), sortable: true },
                { title: _("Scope"), sortable: true },
                { title: _("Filter"), sortable: true },
                { title: _("Next Value"), sortable: true }
            ],
        };

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
        const rows = this.props.rows.map(row => [
            row.cn?.[0] || "",
            row.dnascope?.[0] || "",
            row.dnafilter?.[0] || "",
            row.dnanextvalue?.[0] || ""
        ]);
        this.setState({ rows });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn?.[0] || "",
                row.dnascope?.[0] || "",
                row.dnafilter?.[0] || "",
                row.dnanextvalue?.[0] || ""
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("DNA Configurations") }];
            tableRows = [{ cells: [_("No Configurations")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search Configurations")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="dna table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Hostname"), sortable: true },
                { title: _("Port"), sortable: true },
                { title: _("Remaining Values"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [
            row.dnahostname[0],
            row.dnaportnum[0],
            row.dnaremainingvalues[0]
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.dnahostname[0],
                row.dnaportnum[0],
                row.dnaremainingvalues[0]
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(`${rowData[0]}:${rowData[1]}`)
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(`${rowData[0]}:${rowData[1]}`)
        }
    ];

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("DNA Shared Configurations") }];
            tableRows = [{ cells: [_("No Configurations")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search Shared Configurations")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="dna shared table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Definition Name"), sortable: true },
                { title: _("Default Group"), sortable: true },
                { title: _("Scope"), sortable: true },
                { title: _("Filter"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [
            row.cn[0],
            "automemberdefaultgroup" in row ? row.automemberdefaultgroup[0] : "",
            row.automemberscope[0],
            row.automemberfilter[0],
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn[0],
                "automemberdefaultgroup" in row ? row.automemberdefaultgroup[0] : "",
                row.automemberscope[0],
                row.automemberfilter[0],
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Automembership Definitions") }];
            tableRows = [{ cells: [_("No Definitions")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    className="ds-margin-top-lg"
                    placeholder={_("Search Definitions")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="automember def table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Config Name"), sortable: true },
                { title: _("Exclusive Regex"), sortable: true },
                { title: _("Inclusive Regex"), sortable: true },
                { title: _("Target Group"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [
            row.cn[0],
            row.automemberexclusiveregex?.join(", ") || "",
            row.automemberinclusiveregex?.join(", ") || "",
            row.automembertargetgroup?.[0] || ""
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn[0],
                row.automemberexclusiveregex?.join(", ") || "",
                row.automemberinclusiveregex?.join(", ") || "",
                row.automembertargetgroup?.[0] || ""
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Automembership Regular Expressions") }];
            tableRows = [{ cells: [_("No regular expressions")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    className="ds-margin-top-lg"
                    placeholder={_("Search Regular Expressions")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="automember regex table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Config Name"), sortable: true },
                { title: _("Scope"), sortable: true },
                { title: _("Filter"), sortable: true },
                { title: _("Managed Base"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [
            row.cn[0],
            row.originscope?.[0] || "",
            row.originfilter?.[0] || "",
            row.managedbase?.[0] || ""
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn[0],
                row.originscope?.[0] || "",
                row.originfilter?.[0] || "",
                row.managedbase?.[0] || ""
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Managed Entry Definitions") }];
            tableRows = [{ cells: [_("No definitions")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    className="ds-margin-top-lg"
                    placeholder={_("Search Definitions")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="managed def table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Template DN"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [row.entrydn[0]]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [row.entrydn[0]]);
        } else {
            rows = this.state.rows.filter(row =>
                row[0].toLowerCase().includes(val)
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Managed Entry Templates") }];
            tableRows = [{ cells: [_("No templates")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search Templates")}
                    className="ds-margin-top-lg"
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="managed template table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("URL"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [row.url]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [row.url]);
        } else {
            rows = this.state.rows.filter(row =>
                row[0].toLowerCase().includes(val)
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit URL"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete URL"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("Pass-Through Authentication URLs") }];
            tableRows = [{ cells: [_("No URLs")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search")}
                    className="ds-margin-top-lg"
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="passthru url table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Config Name"), sortable: true },
                { title: _("Attribute"), sortable: true },
                { title: _("Map Method"), sortable: true },
                { title: _("Filter"), sortable: true },
            ],
        };

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
        const rows = this.props.rows.map(row => [
            row.cn[0],
            row.pamidattr?.[0] || "",
            row.pamidmapmethod?.[0] || "",
            row.pamfilter?.[0] || ""
        ]);
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => 
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    handleSearchChange(event, value) {
        let rows = [];
        const val = value.toLowerCase();
        
        if (val === "") {
            rows = this.props.rows.map(row => [
                row.cn[0],
                row.pamidattr?.[0] || "",
                row.pamidmapmethod?.[0] || "",
                row.pamfilter?.[0] || ""
            ]);
        } else {
            rows = this.state.rows.filter(row =>
                row.some(cell => cell.toLowerCase().includes(val))
            );
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Config"),
            onClick: () => this.props.editConfig(rowData[0])
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Config"),
            onClick: () => this.props.deleteConfig(rowData[0])
        }
    ];

    render() {
        const rows = JSON.parse(JSON.stringify(this.state.rows));
        let columns = this.state.columns;
        let has_rows = true;
        let tableRows;

        if (rows.length === 0) {
            has_rows = false;
            columns = [{ title: _("PAM Configurations") }];
            tableRows = [{ cells: [_("No PAM configurations")] }];
        } else {
            const startIdx = (this.state.perPage * this.state.page) - this.state.perPage;
            tableRows = rows.splice(startIdx, this.state.perPage);
        }

        return (
            <div>
                <SearchInput
                    placeholder={_("Search")}
                    className="ds-margin-top-lg"
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table 
                    className="ds-margin-top"
                    aria-label="pass config table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th 
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {has_rows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {has_rows && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
