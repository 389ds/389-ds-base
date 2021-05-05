import React from "react";
import {
    DropdownButton,
    MenuItem,
    actionHeaderCellFormatter,
    sortableHeaderCellFormatter,
    tableCellFormatter,
    noop
} from "patternfly-react";
import {
    Button
} from "@patternfly/react-core";
import { DSTable } from "../dsTable.jsx";
import PropTypes from "prop-types";

class PluginTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            searchFilterValue: "",
            fieldsToSearch: ["cn", "nsslapd-pluginType"],
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
                        transforms: [],
                        formatters: [],
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
                        transforms: [],
                        formatters: [],
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
                        transforms: [],
                        formatters: [],
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
                                    <td key={rowData.cn[0]}>
                                        <Button
                                            variant='primary'
                                            onClick={() => {
                                                this.props.loadModalHandler(rowData);
                                            }}
                                        >
                                            View Plugin
                                        </Button>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
        this.getColumns = this.getColumns.bind(this);
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div>
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    rowKey="cn"
                    rows={this.props.rows}
                    toolBarSearchField="Plugins"
                    toolBarDisableLoadingSpinner
                />
            </div>
        );
    }
}

PluginTable.propTypes = {
    rows: PropTypes.array,
    loadModalHandler: PropTypes.func
};

PluginTable.defaultProps = {
    rows: [],
    loadModalHandler: noop
};

class AttrUniqConfigTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Configs",
            fieldsToSearch: ["cn", "uniqueness-attribute-name"],
            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Config Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "uniqueness-attribute-name",
                    header: {
                        label: "Attribute",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "nsslapd-pluginenabled",
                    header: {
                        label: "Enabled",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData.cn[0]);
                                                }}
                                            >
                                                Delete Config
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class LinkedAttributesTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Configs",
            fieldsToSearch: ["cn", "linkType", "managedType", "linkScope"],
            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Config Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "linktype",
                    header: {
                        label: "Link Type",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "managedtype",
                    header: {
                        label: "Managed Type",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "linkscope",
                    header: {
                        label: "Link Scope",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 4,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 4
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Config
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class DNATable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Configs",
            fieldsToSearch: ["cn", "dnanextvalue", "dnafilter", "dnascope"],

            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Config Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "dnascope",
                    header: {
                        label: "Scope",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "dnafilter",
                    header: {
                        label: "Filter",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "dnanextvalue",
                    header: {
                        label: "Next Value",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 4,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 4
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData.cn[0]);
                                                }}
                                            >
                                                Delete Config
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class DNASharedTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Configs",
            fieldsToSearch: ["dnahostname", "dnaportnum", "dnaremainingvalues"],

            columns: [
                {
                    property: "dnahostname",
                    header: {
                        label: "Hostname",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "dnaportnum",
                    header: {
                        label: "Port",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "dnaremainingvalues",
                    header: {
                        label: "Remaining Values",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                                    <td key={rowData.entrydn[0]}>
                                        <DropdownButton
                                            id={rowData.entrydn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Config
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="entrydn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class AutoMembershipDefinitionTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Definitions",
            fieldsToSearch: [
                "cn",
                "automemberdefaultgroup",
                "automemberfilter",
                "automembergroupingattr",
                "automemberscope"
            ],

            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Definition Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "automemberdefaultgroup",
                    header: {
                        label: "Default Group",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "automemberscope",
                    header: {
                        label: "Scope",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "automemberfilter",
                    header: {
                        label: "Filter",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 4,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 4
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Definition
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Definition
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class AutoMembershipRegexTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Configs",
            fieldsToSearch: [
                "cn",
                "automemberexclusiveregex",
                "automemberinclusiveregex",
                "automembertargetgroup"
            ],

            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Config Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "automemberexclusiveregex",
                    header: {
                        label: "Exclusive Regex",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "automemberinclusiveregex",
                    header: {
                        label: "Inclusive Regex",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "automembertargetgroup",
                    header: {
                        label: "Target Group",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 4,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 4
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Regex
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Regex
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class ManagedDefinitionTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Config Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "originscope",
                    header: {
                        label: "Scope",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "originfilter",
                    header: {
                        label: "Filter",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "managedbase",
                    header: {
                        label: "Managed Base",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 4,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 4
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData.cn[0]);
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData.cn[0]);
                                                }}
                                            >
                                                Delete Config
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-lg">
                <DSTable
                    getColumns={this.getColumns}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                    noSearchBar
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
    editConfig: noop,
    deleteConfig: noop
};

class ManagedTemplateTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            columns: [
                {
                    property: "entrydn",
                    header: {
                        label: "Template",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "actions",
                    header: {
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 1
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.entrydn[0]}>
                                        <DropdownButton
                                            id={rowData.entrydn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData.entrydn[0]);
                                                }}
                                            >
                                                Edit Template
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData.entrydn[0]);
                                                }}
                                            >
                                                Delete Template
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-lg">
                <DSTable
                    getColumns={this.getColumns}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                    noSearchBar
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
    editConfig: noop,
    deleteConfig: noop
};

class PassthroughAuthURLsTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "URLs",
            fieldsToSearch: ["url"],

            columns: [
                {
                    property: "url",
                    header: {
                        label: "URL",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "actions",
                    header: {
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 1
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.id[0]}>
                                        <DropdownButton
                                            id={rowData.id[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit URL
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete URL
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="id"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
};

class PassthroughAuthConfigsTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);

        this.state = {
            searchField: "Configs",
            fieldsToSearch: ["cn", "pamfilter", "pamidattr", "pamidmapmethod"],

            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Config Name",
                        props: {
                            index: 0,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "pamidattr",
                    header: {
                        label: "Attribute",
                        props: {
                            index: 1,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "pamidmapmethod",
                    header: {
                        label: "Map Method",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
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
                    property: "pamfilter",
                    header: {
                        label: "Filter",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 3
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 4,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 4
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton
                                            id={rowData.cn[0]}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Config
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ]
        };
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey="cn"
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
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
    editConfig: noop,
    deleteConfig: noop
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
