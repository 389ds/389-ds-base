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

class ReferralTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Referral",
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
                        label: "Actions",
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
                                            bsStyle="primary"
                                            onClick={() => {
                                                this.props.loadModalHandler(rowData);
                                            }}
                                        >
                                            Delete Referral
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
                    label: "Referrals",
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
        let refTable;
        if (this.props.rows.length == 0) {
            refTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No referrals"}]}
            />;
        } else {
            refTable = <DSShortTable
                getColumns={this.getColumns}
                rowKey={this.state.rowKey}
                rows={this.props.rows}
                disableLoadingSpinner
            />;
        }
        return (
            <div>
                {refTable}
            </div>
        );
    }
}

class IndexTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            searchField: "Indexes",
            fieldsToSearch: ["name"],
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Attribute",
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
                    property: "types",
                    header: {
                        label: "Index Types",
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
                    property: "matchingrules",
                    header: {
                        label: "Matching Rules",
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
            ],
        };

        if (this.props.editable) {
            this.state.columns.push(
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
                                    <td key={rowData.name[0]}>
                                        <DropdownButton id={rowData.name[0]}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.editIndex(rowData);
                                            }}
                                            >
                                                Edit Index
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.reindexIndex(rowData);
                                            }}
                                            >
                                                Reindex Index
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem eventKey="3" onClick={() => {
                                                this.props.deleteIndex(rowData);
                                            }}
                                            >
                                                Delete Index
                                            </MenuItem>
                                        </DropdownButton>
                                    </td>
                                ];
                            }
                        ]
                    }
                }
            );
        }
        this.getColumns = this.getColumns.bind(this);
    } // Constructor

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
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    disableLoadingSpinner
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />
            </div>

        );
    }
}

class EncryptedAttrTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Encrypted Attribute",
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
                        label: "Actions",
                        props: {
                            index: 1,
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
                                    <td key={rowData.name[0]}>
                                        <Button
                                            bsStyle="primary"
                                            onClick={() => {
                                                this.props.loadModalHandler(rowData);
                                            }}
                                        >
                                            Delete Attribute
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
                    label: "Encrypted Attributes",
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
        let attrTable;
        if (this.props.rows.length == 0) {
            attrTable =
                <DSShortTable
                    getColumns={this.getSingleColumn}
                    rowKey={"msg"}
                    rows={[{msg: "No encrypted attributes"}]}
                />;
        } else {
            attrTable =
                <DSShortTable
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    disableLoadingSpinner
                />;
        }
        return (
            <div>
                {attrTable}
            </div>
        );
    }
}

class LDIFTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "LDIF File",
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
                    property: "date",
                    header: {
                        label: "Creation Date",
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
                    property: "size",
                    header: {
                        label: "Size",
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
                                    <td key={rowData.name[0]}>
                                        <Button
                                            bsStyle="primary"
                                            onClick={() => {
                                                this.props.confirmImport(rowData);
                                            }}
                                        >
                                            Import
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
                    label: "LDIF Files",
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
        let LDIFTable;
        if (this.props.rows.length == 0) {
            LDIFTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No LDIF files"}]}
            />;
        } else {
            LDIFTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }
        return (
            <div>
                {LDIFTable}
            </div>
        );
    }
}

class LDIFManageTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "LDIF File",
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
                    property: "suffix",
                    header: {
                        label: "Suffix",
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
                    property: "date",
                    header: {
                        label: "Creation Date",
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
                    property: "size",
                    header: {
                        label: "Size",
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
                                    <td key={rowData.name[0]}>
                                        <DropdownButton id={rowData.name[0]}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.confirmImport(rowData);
                                            }}
                                            >
                                                Import LDIF File
                                            </MenuItem>

                                            <MenuItem divider />
                                            <MenuItem eventKey="3" onClick={() => {
                                                this.props.confirmDelete(rowData);
                                            }}
                                            >
                                                Delete LDIF File
                                            </MenuItem>
                                        </DropdownButton>
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
                    label: "LDIF Files",
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
        let LDIFTable;
        if (this.props.rows.length == 0) {
            LDIFTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No LDIF files"}]}
            />;
        } else {
            LDIFTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }
        return (
            <div>
                {LDIFTable}
            </div>
        );
    }
}

class BackupTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Backup",
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
                    property: "date",
                    header: {
                        label: "Creation Date",
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
                    property: "size",
                    header: {
                        label: "Size",
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
                                    <td key={rowData.name[0]}>
                                        <DropdownButton id={rowData.name[0]}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.confirmRestore(rowData);
                                            }}
                                            >
                                                Restore Backup
                                            </MenuItem>

                                            <MenuItem divider />
                                            <MenuItem eventKey="3" onClick={() => {
                                                this.props.confirmDelete(rowData);
                                            }}
                                            >
                                                Delete Backup
                                            </MenuItem>
                                        </DropdownButton>
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
                    label: "Backup",
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
        let backupTable;
        if (this.props.rows.length == 0) {
            backupTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No Backups"}]}
            />;
        } else {
            backupTable =
                <DSTable
                    id="backupTable"
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }
        return (
            <div>
                {backupTable}
            </div>
        );
    }
}

export class PwpTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "targetdn",
            columns: [
                {
                    property: "targetdn",
                    header: {
                        label: "Target DN",
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
                    property: "pwp_type",
                    header: {
                        label: "Policy Type",
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
                    property: "basedn",
                    header: {
                        label: "Suffix",
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
                                    <td key={rowData.targetdn}>
                                        <DropdownButton id={rowData.targetdn}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.editPolicy(
                                                    rowData.targetdn,
                                                );
                                            }}
                                            >
                                                Edit Policy
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.deletePolicy(rowData.targetdn);
                                            }}
                                            >
                                                Delete Policy
                                            </MenuItem>
                                        </DropdownButton>
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
                    label: "Local Password Policies",
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
        let PwpTable;
        if (this.props.rows.length == 0) {
            PwpTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No Policies"}]}
            />;
        } else {
            PwpTable =
                <DSTable
                    noSearchBar
                    getColumns={this.getColumns}
                    rowKey={this.state.rowKey}
                    rows={this.props.rows}
                    toolBarPagination={[6, 12, 24, 48, 96]}
                    toolBarPaginationPerPage={6}
                />;
        }
        return (
            <div>
                {PwpTable}
            </div>
        );
    }
}

// Property types and defaults

PwpTable.propTypes = {
    rows: PropTypes.array,
    editPolicy: PropTypes.func,
    deletePolicy: PropTypes.func
};

PwpTable.defaultProps = {
    rows: [],
    editPolicy: noop,
    deletePolicy: noop
};

BackupTable.propTypes = {
    rows: PropTypes.array,
    confirmRestore: PropTypes.func,
    confirmDelete: PropTypes.func
};

BackupTable.defaultProps = {
    rows: [],
    confirmRestore: noop,
    confirmDelete: noop
};

LDIFTable.propTypes = {
    rows: PropTypes.array,
    confirmImport: PropTypes.func,
};

LDIFTable.defaultProps = {
    rows: [],
    confirmImport: noop
};

LDIFManageTable.propTypes = {
    rows: PropTypes.array,
    confirmImport: PropTypes.func,
    confirmDelete: PropTypes.func
};

LDIFManageTable.defaultProps = {
    rows: [],
    confirmImport: noop,
    confirmDelete: noop
};

ReferralTable.propTypes = {
    rows: PropTypes.array,
    loadModalHandler: PropTypes.func
};

ReferralTable.defaultProps = {
    rows: [],
    loadModalHandler: noop
};

IndexTable.propTypes = {
    editable: PropTypes.bool,
    rows: PropTypes.array,
    editIndex: PropTypes.func,
    reindexIndex: PropTypes.func,
    deleteIndex: PropTypes.func,
};

IndexTable.defaultProps = {
    editable: false,
    rows: [],
    editIndex: noop,
    reindexIndex: noop,
    deleteIndex: noop,
};

EncryptedAttrTable.propTypes = {
    loadModalHandler: PropTypes.func,
    rows: PropTypes.array,
};

EncryptedAttrTable.defaultProps = {
    loadModalHandler: noop,
    rows: [],
};

export {
    ReferralTable,
    IndexTable,
    EncryptedAttrTable,
    LDIFTable,
    LDIFManageTable,
    BackupTable
};
