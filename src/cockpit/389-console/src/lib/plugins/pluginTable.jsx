import React from "react";
import {
    Button,
    noop,
    actionHeaderCellFormatter,
    sortableHeaderCellFormatter,
    tableCellFormatter,
} from "patternfly-react";
import PropTypes from "prop-types";
import { DSTable } from "../dsTable.jsx";
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
            ],
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
    loadModalHandler: PropTypes.func,
};

PluginTable.defaultProps = {
    rows: [],
    loadModalHandler: noop,
};

export default PluginTable;
