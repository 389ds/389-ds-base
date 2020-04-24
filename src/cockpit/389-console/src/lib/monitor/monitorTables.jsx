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
import { get_date_string, searchFilter } from "../tools.jsx";

class AbortCleanALLRUVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Task Name",
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
                    property: "created",
                    header: {
                        label: "Created",
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
                    property: "rid",
                    header: {
                        label: "Replica ID",
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
                    property: "status",
                    header: {
                        label: "Status",
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
                    property: "log",
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
                                    <td key={rowData.name[0]}>
                                        <Button
                                            bsStyle="primary"
                                            onClick={() => {
                                                this.props.viewLog(this.props.viewLog(rowData.name));
                                            }}
                                        >
                                            View Log
                                        </Button>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ],
        };

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Abort CleanAllRUV Tasks",
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

    render() {
        let rows = [];
        for (let task of this.props.tasks) {
            rows.push({
                'name': task.cn,
                'created': get_date_string(task.nstaskcreated),
                'rid': task['replica-id'],
                'status': task.nstaskstatus,
            });
        }
        let taskTable;
        if (rows.length < 1) {
            taskTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No Tasks"}]}
                />;
        } else {
            taskTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }

        return (
            <div className="ds-margin-top-xlg">
                {taskTable}
            </div>
        );
    }
}

class CleanALLRUVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Task Name",
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
                    property: "created",
                    header: {
                        label: "Created",
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
                    property: "rid",
                    header: {
                        label: "Replica ID",
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
                    property: "status",
                    header: {
                        label: "Status",
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
                    property: "log",
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
                                    <td key={rowData.name[0]}>
                                        <Button
                                            bsStyle="primary"
                                            onClick={() => {
                                                this.props.viewLog(rowData.name);
                                            }}
                                        >
                                            View Log
                                        </Button>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            ],
        };

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "CleanAllRUV Tasks",
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

    render() {
        let rows = [];
        for (let task of this.props.tasks) {
            // task.attrs.nstaskcreated[0]
            rows.push({
                'name': task.attrs.cn[0],
                'created': get_date_string(task.attrs.nstaskcreated[0]),
                'rid': task.attrs['replica-id'][0],
                'status': task.attrs.nstaskstatus[0],
            });
        }

        let taskTable;
        if (rows.length < 1) {
            taskTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No Tasks"}]}
                />;
        } else {
            taskTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }
        return (
            <div className="ds-margin-top-xlg">
                {taskTable}
            </div>
        );
    }
}

class WinsyncAgmtTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            fieldsToSearch: ["agmt-name", "replica", "replica-enabled"],
            rowKey: "agmt-name",
            columns: [
                {
                    property: "agmt-name",
                    header: {
                        label: "Agreement",
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
                    property: "replica",
                    header: {
                        label: "Windows Replica",
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
                    property: "replica-enabled",
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
                    property: "last-update-status",
                    header: {
                        label: "Update Status",
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
                    property: "number-changes-sent",
                    header: {
                        label: "Changes Sent",
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
                    property: "actions",
                    header: {
                        props: {
                            index: 5,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 5
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData['agmt-name'][0]}>
                                        <DropdownButton id={rowData['agmt-name'][0]}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.viewAgmt(rowData['agmt-name'][0]);
                                            }}>
                                                View Agreement Details
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.pokeAgmt(rowData['agmt-name'][0]);
                                            }}>
                                                Poke Agreement
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
        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Replication Winsync Agreements",
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

    render() {
        let agmtTable;
        if (this.props.agmts.length < 1) {
            agmtTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No agreements"}]}
                />;
        } else {
            agmtTable =
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey={this.state.rowKey}
                    rows={this.props.agmts}
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

class AgmtTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            fieldsToSearch: ["agmt-name", "replica", "replica-enabled"],
            rowKey: "agmt-name",
            columns: [
                {
                    property: "agmt-name",
                    header: {
                        label: "Agreement",
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
                    property: "replica",
                    header: {
                        label: "Replica",
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
                    property: "replica-enabled",
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
                    property: "last-update-status",
                    header: {
                        label: "Update Status",
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
                    property: "number-changes-sent",
                    header: {
                        label: "Changes Sent",
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
                    property: "actions",
                    header: {
                        props: {
                            index: 5,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 5
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData['agmt-name'][0]}>
                                        <DropdownButton id={rowData['agmt-name'][0]}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.viewAgmt(rowData['agmt-name'][0]);
                                            }}>
                                                View Agreement Details
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.pokeAgmt(rowData['agmt-name'][0]);
                                            }}>
                                                Poke Agreement
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
        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Replication Agreements",
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

    render() {
        let agmtTable = "";
        if (this.props.agmts.length < 1) {
            agmtTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No agreements"}]}
                />;
        } else {
            agmtTable =
                <DSTable
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.agmts}
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

class ConnectionTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "connid",
            columns: [
                {
                    property: "date",
                    header: {
                        label: "Connection Opened",
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
                    property: "ip",
                    header: {
                        label: "IP Address",
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
                    property: "connid",
                    header: {
                        label: "ID",
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
                    property: "opStarted",
                    header: {
                        label: "Ops Started",
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
                    property: "opCompleted",
                    header: {
                        label: "Ops Finished",
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
                    property: "binddn",
                    header: {
                        label: "Bind DN",
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
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "readwrite",
                    header: {
                        label: "Read/Write",
                        props: {
                            index: 6,
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
                            index: 6
                        },
                        formatters: [tableCellFormatter]
                    }
                },
            ],
        };

        this.getColumns = this.getColumns.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    render() {
        // connection: %s:%s:%s:%s:%s:%s:%s:%s:%s:%s
        //
        // parts:
        //   0 = file descriptor
        //   1 = connection start date
        //   2 = ops initiated
        //   3 = ops completed
        //   4 = r/w blocked
        //   5 = bind DN
        //   6 = connection is currently at max threads (1 = yes, 0 = no)
        //   7 = number of times connection hit max threads
        //   8 = number of operations blocked by max threads
        //   9 = connection ID
        //   10 = IP address (ip=###################)
        //
        // This is too many items to fit in the table, we have to pick and choose
        // what "we" think are the most useful stats...

        let rows = [];
        for (let conn of this.props.conns) {
            let parts = conn.split(':');

            // Process the IP address
            let ip = parts[10].replace("ip=", "");
            if (ip == "local") {
                ip = "LDAPI";
            }
            rows.push({
                'date': [get_date_string(parts[1])],
                'ip': [ip],
                'connid': [parts[9]],
                'opStarted': [parts[2]],
                'opCompleted': [parts[3]],
                'binddn': [parts[5]],
                'readwrite': [parts[4]]
            });
        }

        return (
            <div className="ds-margin-top-xlg">
                <DSTable
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={12}
                />
            </div>
        );
    }
}

class LagReportTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "agmt-name",
            columns: [
                {
                    property: "agmt-name",
                    header: {
                        label: "Agreement",
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
                    property: "replica-enabled",
                    header: {
                        label: "Enabled",
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
                    property: "replication-status",
                    header: {
                        label: "State",
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true,
                            style: {
                                color: 'blue'
                            }
                        },
                        transforms: [],
                        formatters: [],
                        customFormatters: [sortableHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 2,
                        },
                        formatters: [tableCellFormatter],
                    },
                },
                {
                    property: "replication-lag-time",
                    header: {
                        label: "Lag Time",
                        props: {
                            index: 3,
                            rowSpan: 1,
                            colSpan: 1,
                            sort: true,
                            style: {
                                color: 'blue'
                            },
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
                                    <td key={rowData['agmt-name'][0]}>
                                        <DropdownButton id={rowData['agmt-name'][0]}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.viewAgmt(rowData['agmt-name'][0]);
                                            }}>
                                                View Agreement Details
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.pokeAgmt(rowData['agmt-name'][0]);
                                            }}>
                                                Poke Agreement
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
        this.getColumns = this.getColumns.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div>
                <DSTable
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.agmts}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />
            </div>
        );
    }
}

class GlueTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            fieldsToSearch: ["dn", "created"],
            rowKey: "dn",
            columns: [
                {
                    property: "dn",
                    header: {
                        label: "Glue Entry",
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
                    property: "desc",
                    header: {
                        label: "Conflict Description",
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
                    property: "created",
                    header: {
                        label: "Created",
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
                                    <td key={rowData.dn[0]}>
                                        <DropdownButton id={rowData.dn[0]}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.convertGlue(rowData.dn[0]);
                                            }}>
                                                Convert Glue Entry
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.deleteGlue(rowData.dn[0]);
                                            }}>
                                                Delete Glue Entry
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
        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Replication Glue Entries",
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

    render() {
        let glueTable;
        if (this.props.glues.length < 1) {
            glueTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No glue entries"}]}
                />;
        } else {
            let rows = [];
            for (let glue of this.props.glues) {
                rows.push({
                    dn: [glue.dn],
                    desc: glue.attrs.nsds5replconflict,
                    created: [get_date_string(glue.attrs.createtimestamp[0])],
                });
            }

            glueTable =
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey={this.state.rowKey}
                    rows={rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }

        return (
            <div className="ds-margin-top-xlg">
                {glueTable}
            </div>
        );
    }
}

class ConflictTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            fieldsToSearch: ["dn", "desc"],
            rowKey: "dn",
            columns: [
                {
                    property: "dn",
                    header: {
                        label: "Conflict DN",
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
                    property: "desc",
                    header: {
                        label: "Conflict Description",
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
                    property: "created",
                    header: {
                        label: "Created",
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
                                    <td key={rowData.dn[0]}>
                                        <Button
                                            bsStyle="primary"
                                            onClick={() => {
                                                this.props.resolveConflict(rowData.dn[0]);
                                            }}
                                        >
                                            Resolve
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
    } // Constructor

    getColumns() {
        return this.state.columns;
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Replication Conflict Entries",
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

    render() {
        let conflictTable;
        if (this.props.conflicts.length < 1) {
            conflictTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No conflict entries"}]}
                />;
        } else {
            let rows = [];
            for (let conflict of this.props.conflicts) {
                rows.push({
                    dn: [conflict.dn],
                    desc: conflict.attrs.nsds5replconflict,
                    created: [get_date_string(conflict.attrs.createtimestamp[0])],
                });
            }

            conflictTable =
                <DSTable
                    getColumns={this.getColumns}
                    fieldsToSearch={this.state.fieldsToSearch}
                    toolBarSearchField={this.state.searchField}
                    rowKey={this.state.rowKey}
                    rows={rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }

        return (
            <div className="ds-margin-top-xlg">
                {conflictTable}
            </div>
        );
    }
}

class DiskTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "mount",
            columns: [
                {
                    property: "mount",
                    header: {
                        label: "Disk Partition",
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
                    property: "size",
                    header: {
                        label: "Disk Size",
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
                    property: "used",
                    header: {
                        label: "Used Space",
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
                    property: "avail",
                    header: {
                        label: "Available Space",
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

            ]
        };
        this.getColumns = this.getColumns.bind(this);
    }

    getColumns() {
        return this.state.columns;
    }

    render() {
        return (
            <div className="ds-margin-top-xlg">
                <DSShortTable
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.disks}
                    disableLoadingSpinner
                />
            </div>
        );
    }
}

class ReportAliasesTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);

        this.state = {
            searchField: "Aliases",
            fieldsToSearch: ["alias", "connData"],

            columns: [
                {
                    property: "alias",
                    header: {
                        label: "Alias",
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
                    property: "connData",
                    header: {
                        label: "Connection Data",
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
                    property: "actions",
                    header: {
                        props: {
                            index: 2,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 2
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.alias}>
                                        <DropdownButton
                                            id={rowData.alias}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Alias
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Alias
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

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Instance Aliases",
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

    render() {
        let reportAliasTable;
        if (this.props.rows.length < 1) {
            reportAliasTable = (
                <DSShortTable
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{ msg: "No alias entries" }]}
                    disableLoadingSpinner
                />
            );
        } else {
            reportAliasTable = (
                <DSShortTable
                    getColumns={this.getColumns}
                    rowKey="alias"
                    rows={this.props.rows}
                    disableLoadingSpinner
                />
            );
        }

        return <div className="ds-margin-top-xlg">{reportAliasTable}</div>;
    }
}

class ReportCredentialsTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);

        this.state = {
            columns: [
                {
                    property: "connData",
                    header: {
                        label: "Connection Data",
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
                    property: "credsBinddn",
                    header: {
                        label: "Bind DN",
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
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.connData}>
                                        {value == "" ? <i>Edit To Add a Bind DN Data</i> : value }
                                    </td>
                                ];
                            }
                        ]
                    }
                },
                {
                    property: "credsBindpw",
                    header: {
                        label: "Password",
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
                                let pwField = <i>Interractive Input is set</i>;
                                if (!rowData.pwInputInterractive) {
                                    if (value == "") {
                                        pwField = <i>Both Password or Interractive Input flag are not set</i>;
                                    } else {
                                        pwField = "********";
                                    }
                                }
                                return [
                                    <td key={rowData.connData}>
                                        {pwField}
                                    </td>
                                ];
                            }
                        ]
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
                                    <td key={rowData.connData}>
                                        <DropdownButton
                                            id={rowData.connData}
                                            bsStyle="default"
                                            title="Actions"
                                        >
                                            <MenuItem
                                                eventKey="1"
                                                onClick={() => {
                                                    this.props.editConfig(rowData);
                                                }}
                                            >
                                                Edit Connection
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem
                                                eventKey="2"
                                                onClick={() => {
                                                    this.props.deleteConfig(rowData);
                                                }}
                                            >
                                                Delete Connection
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

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Replica Credentials Table",
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

    render() {
        let reportConnTable;
        if (this.props.rows.length < 1) {
            reportConnTable = (
                <DSShortTable
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{ msg: "No connection entries" }]}
                    disableLoadingSpinner
                />
            );
        } else {
            reportConnTable = (
                <DSShortTable
                    getColumns={this.getColumns}
                    rowKey="connData"
                    rows={this.props.rows}
                    disableLoadingSpinner
                />
            );
        }

        return <div className="ds-margin-top-xlg">{reportConnTable}</div>;
    }
}

class ReportSingleTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);

        this.state = {
            searchField: "Replica",
            fieldsToSearch: [
                "supplierName",
                "replicaName",
                "replicaStatus",
                "agmt-name",
                "replica",
                "replicaStatus",
                "replica-enabled",
                "replication-lag-time"
            ],

            columns: [
                {
                    property: "supplierName",
                    header: {
                        label: "Supplier",
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
                    property: "replicaName",
                    header: {
                        label: "Suffix:ReplicaID",
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
                    property: "replicaStatus",
                    header: {
                        label: "Replica Status",
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
                    property: "agmt-name",
                    header: {
                        label: "Agreement",
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
                                return [
                                    <td key={rowData.rowKey}>
                                        {value || <i>No Agreements Were Found</i>}
                                    </td>
                                ];
                            }
                        ]
                    }
                },
                {
                    property: "replica",
                    header: {
                        label: "Consumer",
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
                    property: "replica-enabled",
                    header: {
                        label: "Is Enabled",
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
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "replication-lag-time",
                    header: {
                        label: "Lag Time",
                        props: {
                            index: 6,
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
                            index: 6
                        },
                        formatters: [tableCellFormatter]
                    }
                },
                {
                    property: "actions",
                    header: {
                        props: {
                            index: 7,
                            rowSpan: 1,
                            colSpan: 1
                        },
                        formatters: [actionHeaderCellFormatter]
                    },
                    cell: {
                        props: {
                            index: 7
                        },
                        formatters: [
                            (value, { rowData }) => {
                                return [
                                    <td key={rowData.rowKey}>
                                        <Button
                                            onClick={() => {
                                                this.props.viewAgmt(rowData['supplierName'][0],
                                                                    rowData['replicaName'][0],
                                                                    rowData['agmt-name'][0]);
                                            }}
                                        >
                                            View Data
                                        </Button>
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

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "All In One Report",
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

    render() {
        let reportSingleTable;
        let filteredRows = this.props.rows;
        if (!this.props.showDisabledAgreements) {
            filteredRows = searchFilter("on", ["replica-enabled"], filteredRows);
        }
        if (filteredRows.length < 1) {
            reportSingleTable = (
                <DSShortTable
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{ msg: "No replica entries" }]}
                    disableLoadingSpinner
                    noSearchBar
                />
            );
        } else {
            reportSingleTable = (
                <DSShortTable
                    getColumns={this.getColumns}
                    rowKey="rowKey"
                    rows={filteredRows}
                    disableLoadingSpinner
                    noSearchBar
                />
            );
        }

        return <div>{reportSingleTable}</div>;
    }
}

class ReportConsumersTable extends React.Component {
    constructor(props) {
        super(props);

        this.getColumns = this.getColumns.bind(this);
        this.getSingleColumn = this.getSingleColumn.bind(this);

        this.state = {
            searchField: "Agreements",
            fieldsToSearch: [
                "agmt-name",
                "replica-enabled",
                "replication-status",
                "replication-lag-time"
            ],

            columns: [
                {
                    property: "agmt-name",
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
                    property: "replica-enabled",
                    header: {
                        label: "Is Enabled",
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
                    property: "replication-status",
                    header: {
                        label: "Replication Status",
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
                    property: "replication-lag-time",
                    header: {
                        label: "Replication Lag Time",
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
                                    <td key={rowData.rowKey}>
                                        <Button
                                            onClick={() => {
                                                this.props.viewAgmt(rowData['supplierName'][0],
                                                                    rowData['replicaName'][0],
                                                                    rowData['agmt-name'][0]);
                                            }}
                                        >
                                            View Data
                                        </Button>
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

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Report Consumers",
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

    render() {
        let reportConsumersTable;
        let filteredRows = this.props.rows;
        if (!this.props.showDisabledAgreements) {
            filteredRows = searchFilter("on", ["replica-enabled"], filteredRows);
        }
        if (filteredRows.length < 1) {
            reportConsumersTable = (
                <DSShortTable
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{ msg: "No agreement entries" }]}
                    disableLoadingSpinner
                    noSearchBar
                />
            );
        } else {
            reportConsumersTable = (
                <DSShortTable
                    getColumns={this.getColumns}
                    rowKey="rowKey"
                    rows={filteredRows}
                    disableLoadingSpinner
                    noSearchBar
                />
            );
        }

        return <div>{reportConsumersTable}</div>;
    }
}
// Proptypes and defaults

LagReportTable.propTypes = {
    agmts: PropTypes.array,
    viewAgmt: PropTypes.func,
    pokeAgmt: PropTypes.func,
};

LagReportTable.defaultProps = {
    agmts: [],
    viewAgmt: noop,
    pokeAgmt: noop
};

AgmtTable.propTypes = {
    agmts: PropTypes.array,
    viewAgmt: PropTypes.func,
    pokeAgmt: PropTypes.func,
};

AgmtTable.defaultProps = {
    agmts: [],
    viewAgmt: noop,
    pokeAgmt: noop
};

WinsyncAgmtTable.propTypes = {
    agmts: PropTypes.array,
    viewAgmt: PropTypes.func,
    pokeAgmt: PropTypes.func,
};

WinsyncAgmtTable.defaultProps = {
    agmts: [],
    viewAgmt: noop,
    pokeAgmt: noop
};

ConnectionTable.propTypes = {
    conns: PropTypes.array,
};

ConnectionTable.defaultProps = {
    conns: [],
};

CleanALLRUVTable.propTypes = {
    tasks: PropTypes.array,
    viewLog: PropTypes.func,
};

CleanALLRUVTable.defaultProps = {
    tasks: [],
    viewLog: PropTypes.func,
};

AbortCleanALLRUVTable.propTypes = {
    tasks: PropTypes.array,
    viewLog: PropTypes.func,
};

AbortCleanALLRUVTable.defaultProps = {
    tasks: [],
    viewLog: PropTypes.func,
};

ConflictTable.propTypes = {
    conflicts: PropTypes.array,
    resolveConflict: PropTypes.func,
};

ConflictTable.defaultProps = {
    conflicts: [],
    resolveConflict: noop,
};

GlueTable.propTypes = {
    glues: PropTypes.array,
    convertGlue: PropTypes.func,
    deleteGlue: PropTypes.func,
};

GlueTable.defaultProps = {
    glues: PropTypes.array,
    convertGlue: noop,
    deleteGlue: noop,
};

ReportCredentialsTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

ReportCredentialsTable.defaultProps = {
    rows: [],
    editConfig: noop,
    deleteConfig: noop
};

ReportAliasesTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

ReportAliasesTable.defaultProps = {
    rows: [],
    editConfig: noop,
    deleteConfig: noop
};

ReportConsumersTable.propTypes = {
    showDisabledAgreements: PropTypes.bool,
    rows: PropTypes.array,
    viewAgmt: PropTypes.func
};

ReportConsumersTable.defaultProps = {
    showDisabledAgreements: false,
    rows: [],
    viewAgmt: noop
};

ReportSingleTable.propTypes = {
    showDisabledAgreements: PropTypes.bool,
    rows: PropTypes.array,
    viewAgmt: PropTypes.func
};

ReportSingleTable.defaultProps = {
    showDisabledAgreements: false,
    rows: [],
    viewAgmt: noop
};

DiskTable.defaultProps = {
    rows: PropTypes.array
};

DiskTable.defaultProps = {
    rows: []
};

export {
    ConnectionTable,
    AgmtTable,
    WinsyncAgmtTable,
    LagReportTable,
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
    ConflictTable,
    GlueTable,
    ReportCredentialsTable,
    ReportAliasesTable,
    ReportConsumersTable,
    ReportSingleTable,
    DiskTable
};
