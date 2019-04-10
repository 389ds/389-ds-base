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
import { get_date_string } from "../tools.jsx";

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
                                    <td key={rowData['agmt-name']}>
                                        <DropdownButton id={rowData['agmt-name']}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.viewAgmt(rowData['agmt-name']);
                                            }}>
                                                View Agreement Details
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.pokeAgmt(rowData['agmt-name']);
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
                                    <td key={rowData['agmt-name']}>
                                        <DropdownButton id={rowData['agmt-name']}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.viewAgmt(rowData['agmt-name']);
                                            }}>
                                                View Agreement Details
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.pokeAgmt(rowData['agmt-name']);
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
                                    <td key={rowData['agmt-name']}>
                                        <DropdownButton id={rowData['agmt-name']}
                                            bsStyle="default" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.viewAgmt(rowData['agmt-name']);
                                            }}>
                                                View Agreement Details
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.pokeAgmt(rowData['agmt-name']);
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

// Proptyes and defaults
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

export {
    ConnectionTable,
    AgmtTable,
    WinsyncAgmtTable,
    LagReportTable,
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
};
