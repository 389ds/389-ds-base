import React from 'react';
import { SelectOption } from '@patternfly/react-core';
import {
    Table,
    TableHeader,
    TableBody,
    TableVariant,
} from '@patternfly/react-table';

class AciBindRuleTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            columns: ['Bind Rule', 'Comparator', 'LDAP URLs'],
            actions: [
                {
                    title: 'Remove Bind Rule',
                    onClick: (event, rowId, rowData, extra) => this.props.removeRow(rowId)
                }
            ],
            rows: [],
        };
    }

    componentDidMount () {
        let columns = ['Bind Rule', 'Comparator', 'LDAP URLs'];
        let rows = [...this.props.rows];
        if (this.props.rows.length === 0) {
            columns = ['Bind Rules'];
            rows = [{ cells: ['No bind rules'] }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    render() {
        const { columns, rows, actions } = this.state;

        return (
            <Table
                className="ds-margin-top-lg"
                actions={this.props.rows.length !== 0 ? actions : null}
                aria-label="bind rule Table"
                variant={TableVariant.compact}
                cells={columns}
                rows={rows}
            >
                <TableHeader />
                <TableBody />
            </Table>
        );
    }
}

export default AciBindRuleTable;
