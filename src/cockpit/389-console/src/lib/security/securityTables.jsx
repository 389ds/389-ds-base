import React from "react";
import {
    // Button,
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

class CertTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "nickname",
            columns: [
                {
                    property: "nickname",
                    header: {
                        label: "Certificate Name",
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
                    property: "subject",
                    header: {
                        label: "Subject DN",
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
                    property: "issuer",
                    header: {
                        label: "Issued By",
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
                    property: "flags",
                    header: {
                        label: "Trust Flags",
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
                    property: "expires",
                    header: {
                        label: "Expiration Date",
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
                    property: "action",
                    header: {
                        label: "",
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
                                    <td key={rowData.nickname[0]}>
                                        <DropdownButton id={rowData.nickname[0]}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.editCert(rowData);
                                            }}
                                            >
                                                Edit Trust Flags
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.delCert(rowData);
                                            }}
                                            >
                                                Delete Certificate
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
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Certificates",
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
        let certRows = [];
        let serverTable;
        for (let cert of this.props.certs) {
            let obj = {
                'nickname': [cert.attrs['nickname']],
                'subject': [cert.attrs['subject']],
                'issuer': [cert.attrs['issuer']],
                'expires': [cert.attrs['expires']],
                'flags': [cert.attrs['flags']],
            };
            certRows.push(obj);
        }

        if (certRows.length == 0) {
            serverTable = <DSTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No Certificates"}]}
                key={"nocerts"}
            />;
        } else {
            serverTable = <DSTable
                getColumns={this.getColumns}
                rowKey={this.state.rowKey}
                rows={certRows}
                key={certRows}
                disableLoadingSpinner
            />;
        }

        return (
            <div>
                {serverTable}
            </div>
        );
    }
}

// Future - https://pagure.io/389-ds-base/issue/50491
class CRLTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "name",
            columns: [
                {
                    property: "name",
                    header: {
                        label: "Issued By",
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
                    property: "effective",
                    header: {
                        label: "Effective Date",
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
                    property: "nextUpdate",
                    header: {
                        label: "Next Updateo",
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
                    property: "type",
                    header: {
                        label: "Type",
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
                    property: "action",
                    header: {
                        label: "",
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
                                                this.props.editIndex(rowData);
                                            }}
                                            >
                                                View CRL
                                            </MenuItem>
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.reindexIndex(rowData);
                                            }}
                                            >
                                                Delete CRL
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
    }

    getSingleColumn () {
        return [
            {
                property: "msg",
                header: {
                    label: "Certificate Revocation Lists",
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
        let crlTable;
        if (this.props.rows.length == 0) {
            crlTable = <DSTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "None"}]}
            />;
        } else {
            crlTable = <DSTable
                getColumns={this.getColumns}
                rowKey={this.state.rowKey}
                rows={this.props.rows}
                disableLoadingSpinner
            />;
        }
        return (
            <div>
                {crlTable}
            </div>
        );
    }
}

// Props and defaults

CertTable.propTypes = {
    // serverId: PropTypes.string,
    certs: PropTypes.array,
    editCert: PropTypes.func,
    delCert: PropTypes.func,
};

CertTable.defaultProps = {
    // serverId: "",
    certs: [],
    editCert: noop,
    delCert: noop,
};

export {
    CertTable,
    CRLTable
};
