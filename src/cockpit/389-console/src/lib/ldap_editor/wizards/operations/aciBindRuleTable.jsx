import cockpit from "cockpit";
import React from 'react';
import {
    Table,
    Thead,
    Tbody,
    Tr,
    Th,
    Td,
    ActionsColumn
} from '@patternfly/react-table';

const _ = cockpit.gettext;

class AciBindRuleTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
                columns: [
                    { title: _("Bind Rule") },
                    { title: _("Comparator") },
                    { title: _("LDAP URLs") }
                ],
                rows: [],
            };
    }

    componentDidMount() {
        let columns = [
            { title: _("Bind Rule") },
            { title: _("Comparator") },
            { title: _("LDAP URLs") }
        ];
        let rows = [...this.props.rows];
        if (this.props.rows.length === 0) {
            columns = [{ title: _("Bind Rules") }];
            rows = [{ cells: [_("No bind rules")] }];
        }
        this.setState({
            rows,
            columns
        });
    }

    getActions = (rowIndex) => [
        {
            title: _("Remove Bind Rule"),
            onClick: () => this.props.removeRow(rowIndex)
        }
    ];

    render() {
        const { columns, rows } = this.state;
        const hasRows = this.props.rows.length !== 0;

        return (
            <Table
                className="ds-margin-top-lg"
                aria-label="bind rule Table"
                variant="compact"
            >
                <Thead>
                    <Tr>
                        {columns.map((column, columnIndex) => (
                            <Th key={columnIndex}>{column.title}</Th>
                        ))}
                        {hasRows && <Th screenReaderText="Actions" />}
                    </Tr>
                </Thead>
                <Tbody>
                    {rows.map((row, rowIndex) => (
                        <Tr key={rowIndex}>
                            {Array.isArray(row) ? (
                                // Handle array-type rows
                                row.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>{cell}</Td>
                                ))
                            ) : (
                                // Handle object-type rows (for the "No bind rules" case)
                                row.cells && row.cells.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>{cell}</Td>
                                ))
                            )}
                            {hasRows && (
                                <Td isActionCell>
                                    <ActionsColumn
                                        items={this.getActions(rowIndex)}
                                    />
                                </Td>
                            )}
                        </Tr>
                    ))}
                </Tbody>
            </Table>
        );
    }
}

export default AciBindRuleTable;
