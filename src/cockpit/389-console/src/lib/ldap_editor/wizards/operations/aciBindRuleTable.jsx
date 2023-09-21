import cockpit from "cockpit";
import React from 'react';
import {
    Table,
    TableHeader,
    TableBody,
    TableVariant,
} from '@patternfly/react-table';

const _ = cockpit.gettext;

class AciBindRuleTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            columns: [_("Bind Rule"), _("Comparator"), _("LDAP URLs")],
            actions: [
                {
                    title: _("Remove Bind Rule"),
                    onClick: (event, rowId, rowData, extra) => this.props.removeRow(rowId)
                }
            ],
            rows: [],
        };
    }

    componentDidMount () {
        let columns = [_("Bind Rule"), _("Comparator"), _("LDAP URLs")];
        let rows = [...this.props.rows];
        if (this.props.rows.length === 0) {
            columns = [_("Bind Rules")];
            rows = [{ cells: [_("No bind rules")] }];
        }
        this.setState({
            rows,
            columns
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
