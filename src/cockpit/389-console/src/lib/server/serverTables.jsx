import React from "react";
import {
    DropdownButton,
    MenuItem,
    actionHeaderCellFormatter,
    sortableHeaderCellFormatter,
    tableCellFormatter,
    noop
} from "patternfly-react";
import { DSTable, DSShortTable } from "../dsTable.jsx";
import PropTypes from "prop-types";

export class SASLTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            rowKey: "cn",
            columns: [
                {
                    property: "cn",
                    header: {
                        label: "Mapping Name",
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
                    property: "nssaslmapregexstring",
                    header: {
                        label: "Regular Expression",
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
                    property: "nssaslmapbasedntemplate",
                    header: {
                        label: "Search Base DN",
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
                    property: "nssaslmapfiltertemplate",
                    header: {
                        label: "Search Filter",
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
                    property: "nssaslmappriority",
                    header: {
                        label: "Priority",
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
                                    <td key={rowData.cn[0]}>
                                        <DropdownButton id={rowData.cn[0]}
                                            className="ds-action-button"
                                            bsStyle="primary" title="Actions">
                                            <MenuItem eventKey="1" onClick={() => {
                                                this.props.editMapping(
                                                    rowData.cn[0],
                                                    rowData.nssaslmapregexstring[0],
                                                    rowData.nssaslmapbasedntemplate[0],
                                                    rowData.nssaslmapfiltertemplate[0],
                                                    rowData.nssaslmappriority[0]
                                                );
                                            }}
                                            >
                                                Edit Mapping
                                            </MenuItem>
                                            <MenuItem divider />
                                            <MenuItem eventKey="2" onClick={() => {
                                                this.props.deleteMapping(rowData.cn[0]);
                                            }}
                                            >
                                                Delete Mapping
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
                    label: "SASL Mappings",
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
        let SASLTable;
        if (this.props.rows.length == 0) {
            SASLTable = <DSShortTable
                getColumns={this.getSingleColumn}
                rowKey={"msg"}
                rows={[{msg: "No Mappings"}]}
            />;
        } else {
            SASLTable =
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
                {SASLTable}
            </div>
        );
    }
}

SASLTable.propTypes = {
    rows: PropTypes.array,
    editMapping: PropTypes.func,
    deleteMapping: PropTypes.func
};

SASLTable.defaultProps = {
    rows: [],
    editMapping: noop,
    deleteMapping: noop
};
