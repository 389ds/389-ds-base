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

class ObjectClassesTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            searchFilterValue: "",
            fieldsToSearch: ["name", "oid"],
            columns: [
                {
                    property: "name",
                    header: {
                        label: "ObjectClass Name",
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
                    property: "oid",
                    header: {
                        label: "OID",
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
                    property: "must",
                    header: {
                        label: "Required Attributes",
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
                        formatters: [
                            (value, { rowData }) => {
                                return [<td key={rowData.name[0]}>{value.join(" ")}</td>];
                            }
                        ]
                    }
                },
                {
                    property: "may",
                    header: {
                        label: "Allowed Attributes",
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
                        formatters: [
                            (value, { rowData }) => {
                                return [<td key={rowData.name[0]}>{value.join(" ")}</td>];
                            }
                        ]
                    }
                },
                {
                    property: "actions",
                    header: {
                        label: "Actions",
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
                                    <td key={rowData.name[0]}>
                                        {rowData.x_origin === null ||
                                        rowData.x_origin === undefined ||
                                        rowData.x_origin.length == 0 ||
                                        rowData.x_origin[0].toLowerCase() != "user defined" ? (
                                            <Button
                                                onClick={() => {
                                                    this.props.viewModalHandler(rowData);
                                                }}
                                            >
                                                View Objectclass
                                            </Button>
                                        ) : (
                                            <DropdownButton
                                                id={rowData.name[0]}
                                                bsStyle="default"
                                                title="Choose Action..."
                                                className="ds-schema-dropdown"
                                            >
                                                <MenuItem
                                                    eventKey="1"
                                                    className="ds-schema-dropdown"
                                                    onClick={() => {
                                                        this.props.editModalHandler(rowData);
                                                    }}
                                                >
                                                    Edit ObjectClass
                                                </MenuItem>
                                                <MenuItem divider />
                                                <MenuItem
                                                    eventKey="2"
                                                    className="ds-schema-dropdown"
                                                    onClick={() => {
                                                        this.props.deleteHandler(rowData.name[0]);
                                                    }}
                                                >
                                                    Delete ObjectClass
                                                </MenuItem>
                                            </DropdownButton>
                                        )}
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
                    rowKey="name"
                    rows={this.props.rows}
                    toolBarSearchField="ObjectClasses"
                    toolBarLoading={this.props.loading}
                />
            </div>
        );
    }
}

ObjectClassesTable.propTypes = {
    rows: PropTypes.array,
    editModalHandler: PropTypes.func,
    deleteHandler: PropTypes.func,
    viewModalHandler: PropTypes.func,
    loading: PropTypes.bool
};

ObjectClassesTable.defaultProps = {
    rows: [],
    editModalHandler: noop,
    deleteHandler: noop,
    viewModalHandler: noop,
    loading: false
};

class AttributesTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            searchFilterValue: "",
            fieldsToSearch: ["name", "oid"],
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Attribute Name",
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
                    property: "oid",
                    header: {
                        label: "OID",
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
                    property: "syntax",
                    header: {
                        label: "Syntax",
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
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.name[0]}>
                                        {
                                            this.props.syntaxes.filter(
                                                attr => attr.id === value[0]
                                            )[0]["label"]
                                        }
                                    </td>
                                ];
                            }
                        ]
                    }
                },
                {
                    property: "multivalued",
                    header: {
                        label: "Multivalued",
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
                        formatters: [
                            (value, { rowData }) => {
                                return [<td key={rowData.name[0]}>{value ? "yes" : "no"}</td>];
                            }
                        ]
                    }
                },
                {
                    property: "actions",
                    header: {
                        label: "Actions",
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
                                    <td key={rowData.name[0]}>
                                        {rowData.x_origin === null ||
                                        rowData.x_origin === undefined ||
                                        rowData.x_origin[0].toLowerCase() != "user defined" ? (
                                            <Button
                                                onClick={() => {
                                                    this.props.viewModalHandler(rowData);
                                                }}
                                            >
                                                View Attribute
                                            </Button>
                                        ) : (
                                            <DropdownButton
                                                id={rowData.name[0]}
                                                bsStyle="default"
                                                title="Choose Action..."
                                                className="ds-schema-dropdown"
                                            >
                                                <MenuItem
                                                    eventKey="1"
                                                    className="ds-schema-dropdown"
                                                    onClick={() => {
                                                        this.props.editModalHandler(rowData);
                                                    }}
                                                >
                                                    Edit Attribute
                                                </MenuItem>
                                                <MenuItem divider />
                                                <MenuItem
                                                    eventKey="2"
                                                    className="ds-schema-dropdown"
                                                    onClick={() => {
                                                        this.props.deleteHandler(rowData.name[0]);
                                                    }}
                                                >
                                                    Delete Attribute
                                                </MenuItem>
                                            </DropdownButton>
                                        )}
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
                    rowKey="name"
                    rows={this.props.rows}
                    toolBarSearchField="Attributes"
                    toolBarLoading={this.props.loading}
                />
            </div>
        );
    }
}

AttributesTable.propTypes = {
    rows: PropTypes.array,
    editModalHandler: PropTypes.func,
    deleteHandler: PropTypes.func,
    viewModalHandler: PropTypes.func,
    syntaxes: PropTypes.array,
    loading: PropTypes.bool
};

AttributesTable.defaultProps = {
    rows: [],
    editModalHandler: noop,
    deleteHandler: noop,
    viewModalHandler: noop,
    syntaxes: [],
    loading: false
};

class MatchingRulesTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            searchFilterValue: "",
            fieldsToSearch: ["name", "oid"],
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Matching Rule Name",
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
                    property: "oid",
                    header: {
                        label: "OID",
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
                    property: "syntax",
                    header: {
                        label: "Syntax",
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
                    property: "desc",
                    header: {
                        label: "Description",
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
                    rowKey="oid"
                    rows={this.props.rows}
                    toolBarSearchField="Matching Rules"
                    toolBarDisableLoadingSpinner
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
