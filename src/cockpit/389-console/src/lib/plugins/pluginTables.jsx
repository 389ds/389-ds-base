import React from "react";
import {
    Button,
    DropdownButton,
    MenuItem,
    actionHeaderCellFormatter,
    sortableHeaderCellFormatter,
    tableCellFormatter,
    noop
} from "patternfly-react";
import { DSTable } from "../dsTable.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";

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
                                            onClick={() => {
                                                this.props.loadModalHandler(
                                                    rowData
                                                );
                                            }}
                                        >
                                            Edit Plugin
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
                                                    this.props.editConfig(
                                                        rowData
                                                    );
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(
                                                        rowData
                                                    );
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
                                                    this.props.editConfig(
                                                        rowData
                                                    );
                                                }}
                                            >
                                                Edit Config
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(
                                                        rowData
                                                    );
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

export { PluginTable, AttrUniqConfigTable, LinkedAttributesTable };
