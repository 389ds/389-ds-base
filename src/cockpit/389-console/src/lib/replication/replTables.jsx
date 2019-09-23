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
import { DSTable, DSShortTable } from "../dsTable.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";

class ReplAgmtTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            searchField: "Agreements",
            fieldsToSearch: ["name"],
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Agreement Name",
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
                    property: "host",
                    header: {
                        label: "Replica Hostname",
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
                    property: "port",
                    header: {
                        label: "Replica Port",
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
                    property: "state",
                    header: {
                        label: "State",
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
                    property: "status",
                    header: {
                        label: "Last Update Status",
                        props: {
                            index: 4,
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
                            index: 4
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "initstatus",
                    header: {
                        label: "Last Init Status",
                        props: {
                            index: 5,
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
                            index: 5
                        },
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 6,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 6
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.name[0]}>
                                        <DropdownButton
                                            pullRight
                                            id={rowData.name[0]}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions"
                                        >
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.edit(rowData.name[0]);
                                            }}
                                            >
                                                Edit Agreement
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.init(rowData.name[0]);
                                            }}
                                            >
                                                Initialize Agreement
                                            </MenuItem>
                                            <MenuItem eventKey="3" onClick={() => {
                                                this.props.poke(rowData.name[0]);
                                            }}
                                                title="Awaken agreement if it is sleeping"
                                            >
                                                Poke Agreement
                                            </MenuItem>
                                            <MenuItem eventKey="4" onClick={() => {
                                                this.props.enable(rowData.name[0], rowData.state[0]);
                                            }}
                                            >
                                                Disable/Enable Agreement
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem eventKey="3" onClick={() => {
                                                this.props.delete(rowData.name[0], rowData.state[0]);
                                            }}
                                            >
                                                Delete Agreement
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                },
            ],
        };

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Agreements",
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
        ];
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        let agmtTable;
        if (this.props.rows.length == 0) {
            agmtTable =
                <DSShortTable
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No Agreements"}]}
                />;
        } else {
            agmtTable =
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }

        return (
            <div className="ds-margin-top-xlg">
                {agmtTable}
            </div>

        );
    }
}

class ManagerTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Replication Manager",
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
                        label: "",
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
                                    <td key={rowData.name[0]}>
                                        <Button
                                            bsStyle="default"
                                            onClick={() => {
                                                this.props.confirmDelete(rowData);
                                            }}
                                            title="Delete replication manager"
                                        >
                                            Delete
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
        this.getSingleColumn = this.getSingleColumn.bind(this);
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Replication Managers",
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
        ];
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        let managerTable;
        if (this.props.rows.length == 0) {
            managerTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No Replication Managers"}]}
            />;
        } else {
            managerTable = <DSShortTable
                getColumns={this.getColumns}
                rowKey={this.state.rowKey}
                rows={this.props.rows}
                disableLoadingSpinner
            />;
        }
        return (
            <div>
                {managerTable}
            </div>
        );
    }
}

class RUVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "rid",
            columns: [
                {
                    property: "rid",
                    header: {
                        label: "Replica ID",
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
                    property: "url",
                    header: {
                        label: "Replica LDAP URL",
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
                    property: "maxcsn",
                    header: {
                        label: "Max CSN",
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
                        label: "",
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
                                    <td key={rowData.rid[0]}>
                                        <Button
                                            bsStyle="default"
                                            onClick={() => {
                                                this.props.confirmDelete(rowData.rid[0]);
                                            }}
                                            title="Attempt to clean and remove this Replica ID from this suffix"
                                        >
                                            Clean
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
        this.getSingleColumn = this.getSingleColumn.bind(this);
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Remote RUV's",
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
        ];
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        let ruvTable;
        if (this.props.rows.length == 0) {
            ruvTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No RUV's"}]}
            />;
        } else {
            ruvTable = <DSShortTable
                getColumns={this.getColumns}
                rowKey={this.state.rowKey}
                rows={this.props.rows}
                disableLoadingSpinner
            />;
        }
        return (
            <div>
                {ruvTable}
            </div>
        );
    }
}

ReplAgmtTable.propTypes = {
    rows: PropTypes.array,
    edit: PropTypes.func,
    poke: PropTypes.func,
    init: PropTypes.func,
    enable: PropTypes.func,
    delete: PropTypes.func,
};

ReplAgmtTable.defaultProps = {
    rows: [],
    edit: noop,
    poke: noop,
    init: noop,
    enable: noop,
    delete: noop,
};

ManagerTable.propTypes = {
    rows: PropTypes.array,
    confirmDelete: PropTypes.func
};

ManagerTable.defaultProps = {
    rows: [],
    confirmDelete: noop
};

RUVTable.propTypes = {
    rows: PropTypes.array,
    confirmDelete: PropTypes.func
};

RUVTable.defaultProps = {
    rows: [],
    confirmDelete: noop
};

export {
    ReplAgmtTable,
    ManagerTable,
    RUVTable,
};
