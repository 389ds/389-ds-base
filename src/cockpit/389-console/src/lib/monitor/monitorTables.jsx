import React from "react";
import {
    Button,
    Grid,
    GridItem,
    Pagination,
    PaginationVariant,
    SearchInput,
    Text,
    TextContent,
    TextVariants,
} from '@patternfly/react-core';
import {
    cellWidth,
    expandable,
    Table,
    TableHeader,
    TableBody,
    TableVariant,
    sortable,
    SortByDirection,
    info,
} from '@patternfly/react-table';
import ExclamationTriangleIcon from '@patternfly/react-icons/dist/js/icons/exclamation-triangle-icon';
import PropTypes from "prop-types";
import { get_date_string } from "../tools.jsx";

class AbortCleanALLRUVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Task', transforms: [sortable], cellFormatters: [expandable] },
                { title: 'Created', transforms: [sortable] },
                { title: 'Replica ID', transforms: [sortable] },
                { title: 'Status', transforms: [sortable] },
                { title: '' },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.getLog = this.getLog.bind(this);
    }

    getLog(log) {
        return (
            <TextContent>
                <Text component={TextVariants.h5}>
                    {log}
                </Text>
            </TextContent>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = [...this.state.columns];
        let count = 0;
        for (const task of this.props.tasks) {
            rows.push({
                isOpen: false,
                cells: [
                    task.attrs.cn[0],
                    get_date_string(task.attrs.nstaskcreated[0]),
                    task.attrs['replica-id'][0],
                    task.attrs.nstaskstatus[0],
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getLog(task.attrs.nstasklog[0]) }]
            });
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Tasks'] }];
            columns = [{ title: 'Abort CleanAllRUV Tasks' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_tasks = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const task of this.props.tasks) {
            sorted_tasks.push({
                task: task,
                1: task.attrs.cn[0],
                2: get_date_string(task.attrs.nstaskcreated[0]),
                3: task.attrs['replica-id'][0],
                4: task.attrs.nstaskstatus[0]
            });
        }

        // Sort the connections and build the new rows
        sorted_tasks.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_tasks.reverse();
        }
        for (let task of sorted_tasks) {
            task = task.task;
            rows.push({
                isOpen: false,
                cells: [
                    task.attrs.cn[0],
                    get_date_string(task.attrs.nstaskcreated[0]),
                    task.attrs['replica-id'][0],
                    task.attrs.nstaskstatus[0],
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getLog(task.attrs.nstasklog[0]) }]
            });
            count += 2;
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);
        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable table"
                    cells={columns}
                    rows={tableRows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.tasks.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class CleanALLRUVTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Task', transforms: [sortable], cellFormatters: [expandable] },
                { title: 'Created', transforms: [sortable] },
                { title: 'Replica ID', transforms: [sortable] },
                { title: 'Status', transforms: [sortable] },
                { title: '' },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.getLog = this.getLog.bind(this);
    }

    getLog(log) {
        return (
            <TextContent>
                <Text component={TextVariants.h5}>
                    {log}
                </Text>
            </TextContent>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = [...this.state.columns];
        let count = 0;
        for (const task of this.props.tasks) {
            rows.push({
                isOpen: false,
                cells: [
                    task.attrs.cn[0],
                    get_date_string(task.attrs.nstaskcreated[0]),
                    task.attrs['replica-id'][0],
                    task.attrs.nstaskstatus[0],
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getLog(task.attrs.nstasklog[0]) }]
            });
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Tasks'] }];
            columns = [{ title: 'CleanAllRUV Tasks' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_tasks = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const task of this.props.tasks) {
            sorted_tasks.push({
                task: task,
                1: task.attrs.cn[0],
                2: get_date_string(task.attrs.nstaskcreated[0]),
                3: task.attrs['replica-id'][0],
                4: task.attrs.nstaskstatus[0]
            });
        }

        // Sort the connections and build the new rows
        sorted_tasks.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_tasks.reverse();
        }
        for (let task of sorted_tasks) {
            task = task.task;
            rows.push({
                isOpen: false,
                cells: [
                    task.attrs.cn[0],
                    get_date_string(task.attrs.nstaskcreated[0]),
                    task.attrs['replica-id'][0],
                    task.attrs.nstaskstatus[0],
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getLog(task.attrs.nstasklog[0]) }]
            });
            count += 2;
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);
        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable table"
                    cells={columns}
                    rows={tableRows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.tasks.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class WinsyncAgmtTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Agreement', transforms: [sortable], cellFormatters: [expandable] },
                { title: 'Replica', transforms: [sortable] },
                { title: 'Enabled', transforms: [sortable] },
                { title: '' },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        return (
            <Grid className="ds-indent">
                <GridItem span={3}>Session In Progress:</GridItem>
                <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                <GridItem span={3}>Changes Sent:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-sent'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Init Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Updated Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-update-status'][0] }</b></GridItem>
            </Grid>
        );
    }

    getWakeupButton(name) {
        return (
            <Button
                id={name}
                variant="primary"
                onClick={this.props.pokeAgmt}
                title="Awaken the winsync replication agreement"
                isSmall
            >
                Poke
            </Button>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = [...this.state.columns];
        let count = 0;
        for (const agmt of this.props.agmts) {
            rows.push({
                isOpen: false,
                cells: [
                    agmt['agmt-name'][0],
                    agmt.replica[0],
                    agmt['replica-enabled'][0],
                    { title: this.getWakeupButton(agmt['agmt-name'][0]) }
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(agmt) }]
            });
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Agreements'] }];
            columns = [{ title: 'Winsync Agreements' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const agmt of this.props.agmts) {
            sorted_agmts.push({
                agmt: agmt,
                1: agmt['agmt-name'][0],
                2: agmt.replica[0],
                3: agmt['replica-enabled'][0],
            });
        }

        // Sort the connections and build the new rows
        sorted_agmts.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_agmts.reverse();
        }
        for (let agmt of sorted_agmts) {
            agmt = agmt.agmt;
            rows.push({
                isOpen: false,
                cells: [
                    agmt['agmt-name'][0],
                    agmt.replica[0],
                    agmt['replica-enabled'][0],
                    { title: this.getWakeupButton(agmt['agmt-name'][0]) }
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(agmt) }]
            });
            count += 2;
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;

        // We are using an expandable list, so every row has a child row with an
        // index that points back to the parent.  So when we splice the rows for
        // pagination we have to treat each connection as two rows, and we need
        // to rewrite the child's parent index to point to the correct location
        // in the new spliced array
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);
        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable table"
                    cells={columns}
                    rows={tableRows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.agmts.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class AgmtTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Agreement', transforms: [sortable], cellFormatters: [expandable] },
                { title: 'Replica', transforms: [sortable] },
                { title: 'Enabled', transforms: [sortable] },
                { title: '' },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        return (
            <Grid className="ds-indent">
                <GridItem span={3}>Session In Progress:</GridItem>
                <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                <GridItem span={3}>Changes Sent:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-sent'][0] }</b></GridItem>
                <GridItem span={3}>Changes Skipped:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-skipped'][0] }</b></GridItem>
                <GridItem span={3}>Reap Active:</GridItem>
                <GridItem span={9}><b>{ agmt['reap-active'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Init Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Updated Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-update-status'][0] }</b></GridItem>
            </Grid>
        );
    }

    getWakeupButton(name) {
        return (
            <Button
                id={name}
                variant="primary"
                onClick={this.props.pokeAgmt}
                title="Awaken the replication agreement"
                isSmall
            >
                Poke
            </Button>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = [...this.state.columns];
        let count = 0;
        for (const agmt of this.props.agmts) {
            rows.push({
                isOpen: false,
                cells: [
                    agmt['agmt-name'][0],
                    agmt.replica[0],
                    agmt['replica-enabled'][0],
                    { title: this.getWakeupButton(agmt['agmt-name'][0]) }
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(agmt) }]
            });
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Agreements'] }];
            columns = [{ title: 'Replication Agreements' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const agmt of this.props.agmts) {
            sorted_agmts.push({
                agmt: agmt,
                1: agmt['agmt-name'][0],
                2: agmt.replica[0],
                3: agmt['replica-enabled'][0],
            });
        }

        // Sort the connections and build the new rows
        sorted_agmts.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_agmts.reverse();
        }
        for (let agmt of sorted_agmts) {
            agmt = agmt.agmt;
            rows.push({
                isOpen: false,
                cells: [
                    agmt['agmt-name'][0],
                    agmt.replica[0],
                    agmt['replica-enabled'][0],
                    { title: this.getWakeupButton(agmt['agmt-name'][0]) }
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(agmt) }]
            });
            count += 2;
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        // This is an expandable list
        const { columns, rows, perPage, page, sortBy } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);
        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable table"
                    cells={columns}
                    rows={tableRows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.agmts.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class ConnectionTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                {
                    title: 'Connection Opened',
                    cellFormatters: [expandable],
                    transforms: [sortable]
                },
                { title: 'IP Address', transforms: [sortable] },
                { title: 'Conn ID', transforms: [sortable] },
                { title: 'Bind DN', transforms: [sortable] },
                {
                    title: 'Max Threads',
                    transforms: [
                        info({
                            tooltip: 'If connection is currently at "Max Threads" then it will block new operations'
                        }),
                        sortable
                    ]
                },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(ip, conn_date, parts) {
        return (
            <Grid className="ds-indent">
                <GridItem span={3}>IP Address:</GridItem>
                <GridItem span={4}><b>{ip}</b></GridItem>
                <GridItem span={3}>File Descriptor:</GridItem>
                <GridItem span={2}><b>{parts[0]}</b></GridItem>
                <GridItem span={3}>Connection Opened:</GridItem>
                <GridItem span={4}><b>{conn_date}</b></GridItem>
                <GridItem span={3}>Operations Started:</GridItem>
                <GridItem span={2}><b>{parts[2]}</b></GridItem>
                <GridItem span={3}>Connection ID:</GridItem>
                <GridItem span={4}><b>{parts[9]}</b></GridItem>
                <GridItem span={3}>Operations Finished:</GridItem>
                <GridItem span={2}><b>{parts[3]}</b></GridItem>
                <GridItem span={3}>Bind DN:</GridItem>
                <GridItem span={4}><b>{parts[5]}</b></GridItem>
                <GridItem span={3}>Read/write Blocked:</GridItem>
                <GridItem span={2}><b>{parts[4]}</b></GridItem>
                <hr />
                <GridItem span={6}>Connection Currently At Max Threads:</GridItem>
                <GridItem span={6}><b>{parts[6] == "1" ? "Yes" : "No"}</b></GridItem>
                <GridItem span={6}>Number Of Times Connection Hit Max Threads:</GridItem>
                <GridItem span={6}><b>{parts[7]}</b></GridItem>
                <GridItem span={6}>Number Of Operations Blocked By Max Threads:</GridItem>
                <GridItem span={6}><b>{parts[8]}</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        // connection: %s:%s:%s:%s:%s:%s:%s:%s:%s:ip=%s
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
        const rows = [];
        let count = 0;
        for (const conn of this.props.conns) {
            const ip_parts = conn.split(':ip=');
            const parts = conn.split(':', 10);
            // Process the IP address
            let ip = ip_parts[1];
            if (ip == "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);
            rows.push({
                isOpen: false,
                cells: [
                    conn_date, ip, parts[9], parts[5], parts[6] == "1" ? "Yes" : "No"
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(ip, conn_date, parts) }]
            });
            count += 2;
        }
        this.setState({
            rows: rows
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSearchChange(value, event) {
        const rows = [];
        let count = 0;
        for (const conn of this.props.conns) {
            const ip_parts = conn.split(':ip=');
            const parts = conn.split(':', 10);
            // Process the IP address
            let ip = ip_parts[1];
            if (ip == "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);
            const val = value.toLowerCase();
            const conn_raw = conn.toLowerCase();
            // Check for matches of all the parts
            if (val != "" && conn_raw.indexOf(val) == -1 &&
                ip.toLowerCase().indexOf(val) == -1 &&
                conn_date.indexOf(value) == -1) {
                // Not a match
                continue;
            }
            rows.push({
                isOpen: false,
                cells: [
                    conn_date, ip, parts[9], parts[5], parts[6] == "1" ? "Yes" : "No"
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(ip, conn_date, parts) }]
            });
            count += 2;
        }
        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    onSort(_event, index, direction) {
        const sorted_conns = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const conn of this.props.conns) {
            const ip_parts = conn.split(':ip=');
            const parts = conn.split(':', 10);
            let ip = ip_parts[1];
            if (ip == "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);

            sorted_conns.push({
                raw: conn,
                1: conn_date,
                2: ip,
                3: parts[9],
                4: parts[5],
                5: parts[6]
            });
        }

        // Sort the connections and build the new rows
        sorted_conns.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_conns.reverse();
        }
        for (const conn of sorted_conns) {
            const raw_conn = conn.raw;
            const ip_parts = raw_conn.split(':ip=');
            const parts = raw_conn.split(':', 10);
            // Process the IP address
            let ip = ip_parts[1];
            if (ip == "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);
            rows.push({
                isOpen: false,
                cells: [
                    conn_date, ip, parts[9], parts[5], parts[6] == "1" ? "Yes" : "No"
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(ip, conn_date, parts) }]
            });
            count += 2;
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);
        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            // Rewrite parent index to match new spliced array
            tableRows[idx].parent = count;
        }

        return (
            <div className="ds-margin-top-xlg">
                <TextContent>
                    <Text component={TextVariants.h4}>
                        Client Connections:<b className="ds-left-margin">{this.props.conns.length}</b>
                    </Text>
                </TextContent>
                <SearchInput
                    className="ds-margin-top-xlg"
                    placeholder='Search connections'
                    value={this.state.value}
                    onChange={this.onSearchChange}
                    onClear={(evt) => this.onSearchChange('', evt)}
                />
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable table"
                    cells={columns}
                    rows={tableRows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.conns.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={this.state.perPage}
                    page={this.state.page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class GlueTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            dropdownIsOpen: false,
            columns: [
                { title: 'Glue Entry', transforms: [sortable, cellWidth(12)] },
                { title: 'Description', transforms: [sortable] },
                { title: 'Created', transforms: [sortable] },
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage,
                page: 1
            });
        };

        this.onSort = this.onSort.bind(this);
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const glue of this.props.glues) {
            rows.push({
                cells: [
                    glue.dn, glue.attrs.nsds5replconflict[0], get_date_string(glue.attrs.createtimestamp[0])
                ]
            });
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_glues = [];
        const rows = [];

        // Convert the conns into a sortable array
        for (const glue of this.props.glues) {
            sorted_glues.push({
                1: glue.dn,
                2: glue.attrs.nsds5replconflict[0],
                3: get_date_string(glue.attrs.createtimestamp[0]),
            });
        }

        // Sort the connections and build the new rows
        sorted_glues.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_glues.reverse();
        }
        for (const glue of sorted_glues) {
            rows.push({
                isOpen: false,
                cells: [
                    glue['1'], glue['2'], glue['3']
                ]
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    actions() {
        return [
            {
                title: 'Convert Glue Entry',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.convertGlue(rowData.cells[0])
            },
            {
                title: 'Delete Glue Entry',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteGlue(rowData.cells[0])
            }
        ];
    }

    render() {
        const { perPage, page, sortBy } = this.state;
        let rows = JSON.parse(JSON.stringify(this.state.rows)); // Deep copy
        let columns = this.state.columns;
        let has_rows = true;
        if (rows.length == 0) {
            has_rows = false;
            rows = [{ cells: ['No Glue Entries'] }];
            columns = [{ title: 'Replication Conflict Glue Entries' }];
        }

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="glue table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.glues.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class ConflictTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Conflict DN', transforms: [sortable] },
                { title: 'Description', transforms: [sortable] },
                { title: 'Created', transforms: [sortable] },
                { title: '' }
            ],
        };

        this.onSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.onPerPageSelect = (_event, perPage) => {
            this.setState({
                perPage: perPage
            });
        };

        this.onSort = this.onSort.bind(this);
        this.getResolveButton = this.getResolveButton.bind(this);
    }

    getResolveButton(name) {
        return (
            <Button
                id={name}
                variant="primary"
                isSmall
                onClick={() => {
                    this.props.resolveConflict(name);
                }}
            >
                Resolve
            </Button>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const conflict of this.props.conflicts) {
            rows.push({
                isOpen: false,
                cells: [
                    conflict.dn, conflict.attrs.nsds5replconflict[0], get_date_string(conflict.attrs.createtimestamp[0]),
                    { title: this.getResolveButton(conflict.dn) }
                ]
            });
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Conflict Entries'] }];
            columns = [{ title: 'Replication Conflict Entries' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        const sorted_conflicts = [];
        const rows = [];

        // Convert the conns into a sortable array
        for (const conflict of this.props.conflicts) {
            sorted_conflicts.push({
                1: conflict.dn,
                2: conflict.attrs.nsds5replconflict[0],
                3: get_date_string(conflict.attrs.createtimestamp[0]),
            });
        }

        // Sort the connections and build the new rows
        sorted_conflicts.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_conflicts.reverse();
        }
        for (const conflict of sorted_conflicts) {
            rows.push({
                isOpen: false,
                cells: [
                    conflict['1'], conflict['2'], conflict['3'],
                    { title: this.getResolveButton(conflict['1']) }
                ]
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="conflict table"
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
                <Pagination
                    itemCount={this.props.conflicts.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant={PaginationVariant.bottom}
                    onSetPage={this.onSetPage}
                    onPerPageSelect={this.onPerPageSelect}
                />
            </div>
        );
    }
}

class DiskTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortBy: {},
            columns: [
                { title: 'Disk Partition', transforms: [sortable] },
                { title: 'Disk Size', transforms: [sortable] },
                { title: 'Used Space', transforms: [sortable] },
                { title: 'Available Space', transforms: [sortable] },
            ],
        };
        this.onSort = this.onSort.bind(this);
    }

    onSort(_event, index, direction) {
        const sortedRows = this.props.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    render() {
        const { columns, sortBy } = this.state;

        return (
            <div className="ds-margin-top-xlg">
                <Table aria-label="Sortable Table" sortBy={sortBy} onSort={this.onSort} cells={columns} rows={this.props.rows}>
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}

class ReportAliasesTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            sortBy: {},
            dropdownIsOpen: false,
            columns: [
                { title: 'Alias', transforms: [sortable] },
                { title: 'Connection Data', transforms: [sortable] },
            ],
        };
    }

    actions() {
        return [
            {
                title: 'Edit Alias',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData[0], rowData[1])
            },
            {
                title: 'Delete Alias',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        let columns = this.state.columns;
        let rows = JSON.parse(JSON.stringify(this.props.rows)); // Deep copy
        let has_rows = true;
        if (rows.length == 0) {
            has_rows = false;
            rows = [{ cells: ['No Aliases'] }];
            columns = [{ title: 'Instance Aliases' }];
        }

        return (
            <div className="ds-margin-top-xlg">
                <TextContent>
                    <Text className="ds-center ds-margin-bottom" component={TextVariants.h4}>
                        Replica Naming Aliases
                    </Text>
                </TextContent>
                <Table
                    variant={TableVariant.compact} aria-label="Sortable Table"
                    sortBy={this.props.sortBy} onSort={this.props.onSort} cells={columns}
                    rows={rows}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}

class ReportCredentialsTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            dropdownIsOpen: false,
            columns: [
                { title: 'Connection Data', transforms: [sortable] }, // connData
                { title: 'Bind DN', transforms: [sortable] }, // credsBinddn
                { title: 'Password', transforms: [sortable] }, // credsBindpw
            ],
        };
    }

    actions() {
        return [
            {
                title: 'Edit Connection',
                onClick: (event, rowId, rowData, extra) =>
                    this.props.editConfig(rowData.cells[0], rowData.cells[1], rowData.credsBindpw, rowData.pwInteractive)
            },
            {
                title: 'Delete Connection',
                onClick: (event, rowId, rowData, extra) => this.props.deleteConfig(rowData.cells[0])
            }
        ];
    }

    render() {
        let columns = this.state.columns;
        let rows = [];
        let has_rows = true;
        if (this.props.rows.length == 0) {
            has_rows = false;
            rows = [{ cells: ['No Credentials'] }];
            columns = [{ title: 'Credentials Table' }];
        } else {
            for (let row of this.props.rows) {
                row = JSON.parse(JSON.stringify(row)); // Deep copy
                const pwInteractive = row.pwInputInterractive;
                let pwField = <i>Interactive Input is set</i>;
                if (!pwInteractive) {
                    if (row.credsBindpw == "") {
                        pwField = <i>Both Password or Interactive Input flag are not set</i>;
                    } else {
                        pwField = "********";
                    }
                }
                rows.push(
                    {
                        cells: [
                            row.connData,
                            row.credsBinddn,
                            { title: pwField },
                        ],
                        credsBindpw: row.credsBindpw,
                        pwInteractive: pwInteractive,
                    }
                );
            }
        }

        return (
            <div className="ds-margin-top">
                <TextContent>
                    <Text className="ds-center ds-margin-bottom" component={TextVariants.h4}>
                        Replication Report Credentials
                    </Text>
                </TextContent>
                <Table
                    variant={TableVariant.compact} aria-label="Cred Table"
                    sortBy={this.props.sortBy} onSort={this.props.onSort} cells={columns}
                    rows={rows}
                    actions={has_rows ? this.actions() : null}
                    dropdownPosition="right"
                    dropdownDirection="bottom"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}

class ReportSingleTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Supplier', transforms: [sortable], cellFormatters: [expandable] },
                { title: 'Agreement', transforms: [sortable] },
                { title: 'Status', transforms: [sortable] },
                { title: 'Lag', transforms: [sortable] },
            ],
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        if (agmt['agmt-name'][0] == "-") {
            return (
                <TextContent>
                    <Text component={TextVariants.h4}>
                        No agreement information
                    </Text>
                </TextContent>
            );
        }
        let replEnabled;
        if (agmt['replica-enabled'] == "off") {
            replEnabled =
                <div className="ds-warning-icon">
                    {agmt['replica-enabled'][0]} <ExclamationTriangleIcon />
                </div>;
        } else {
            replEnabled = agmt['replica-enabled'][0];
        }
        return (
            <Grid className="ds-indent">
                <GridItem span={3}>Suffix & Replica ID:</GridItem>
                <GridItem span={9}><b>{ agmt.replicaName[0] }</b></GridItem>
                <GridItem span={3}>Replica Server Status:</GridItem>
                <GridItem span={9}><b>{ agmt.replicaStatus[0] }</b></GridItem>
                <GridItem span={3}>Replication Enabled:</GridItem>
                <GridItem span={9}><b>{ replEnabled }</b></GridItem>
                <GridItem span={3}>Session In Progress:</GridItem>
                <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                <GridItem span={3}>Consumer:</GridItem>
                <GridItem span={9}><b>{ agmt.replica[0] }</b></GridItem>
                <GridItem span={3}>Changes Sent:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-sent'][0] }</b></GridItem>
                <GridItem span={3}>Changes Skipped:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-skipped'][0] }</b></GridItem>
                <GridItem span={3}>Reap Active:</GridItem>
                <GridItem span={9}><b>{ agmt['reap-active'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Init Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Updated Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-update-status'][0] }</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = [...this.state.columns];
        let agmtName;
        let count = 0;
        for (const replica of this.props.rows) {
            if (!('agmt-name' in replica)) {
                replica['agmt-name'] = ["-"];
            }
            if (!('replica' in replica)) {
                replica.replica = ["-"];
            }
            if (!('replica-enabled' in replica)) {
                replica['replica-enabled'] = ["-"];
            }
            if (!('replication-lag-time' in replica)) {
                replica['replication-lag-time'] = ["-"];
            }
            if (!('replication-status' in replica)) {
                replica['replication-status'] = ['-'];
            }
            if (replica['replica-enabled'][0] == "off") {
                agmtName =
                    <div className="ds-warning-icon" title="Agreement is disabled">
                        {replica['agmt-name'][0]} <ExclamationTriangleIcon />
                    </div>;
            } else {
                agmtName = replica['agmt-name'][0];
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [
                        replica.supplierName[0],
                        { title: agmtName },
                        replica['replication-status'][0],
                        replica['replication-lag-time'][0],
                    ],
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(replica) }]
                }
            );
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Agreements'] }];
            columns = [{ title: 'Replication Agreements' }];
        }

        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows } = this.state;
        // const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[rowKey].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const agmt of this.props.rows) {
            sorted_agmts.push({
                agmt: agmt,
                1: agmt.supplierName[0],
                2: agmt['agmt-name'][0],
                3: ['replication-status'][0],
                4: agmt['replication-lag-time'][0],
                enabled: agmt['replica-enabled'] !== "off",
            });
        }

        // Sort the connections and build the new rows
        sorted_agmts.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_agmts.reverse();
        }
        for (let agmt of sorted_agmts) {
            let agmtName;
            if (agmt['replica-enabled'] == "off") {
                agmtName =
                    <div className="ds-warning-icon" title="Agreement is disabled">
                        {agmt['agmt-name'][0]} <ExclamationTriangleIcon />
                    </div>;
            } else {
                agmtName = agmt['agmt-name'][0];
            }
            agmt = agmt.agmt;
            rows.push({
                isOpen: false,
                cells: [
                    agmt.supplierName[0],
                    { title: agmtName },
                    agmt['replication-status'][0],
                    agmt['replication-lag-time'][0],
                ],
                enabled: agmt.enabled
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(agmt) }]
            });
            count += 2;
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
        });
    }

    render() {
        // This is an expandable list
        const { columns, rows, sortBy } = this.state;
        return (
            <div className="ds-margin-top-xlg">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable table"
                    cells={columns}
                    rows={rows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}

class ReportConsumersTable extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            sortBy: {},
            rows: [],
            columns: [
                { title: 'Agreement Name', transforms: [sortable], cellFormatters: [expandable] },
                { title: 'Enabled', transforms: [sortable] },
                { title: 'Status', transforms: [sortable] },
                { title: 'Lag', transforms: [sortable] },
            ],
        };

        this.onSort = this.onSort.bind(this);
        this.onCollapse = this.onCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        let replEnabled;
        if (agmt['agmt-name'][0] == "-") {
            return (
                <TextContent>
                    <Text component={TextVariants.h4}>
                        No agreement information
                    </Text>
                </TextContent>
            );
        }
        if (agmt['replica-enabled'] == "off") {
            replEnabled =
                <div className="ds-warning-icon">
                    {agmt['replica-enabled'][0]} <ExclamationTriangleIcon />
                </div>;
        } else {
            replEnabled = agmt['replica-enabled'][0];
        }
        return (
            <Grid className="ds-margin-left">
                <GridItem span={3}>Suffix & Replica ID:</GridItem>
                <GridItem span={9}><b>{ agmt.replicaName[0] }</b></GridItem>
                <GridItem span={3}>Replica Server Status:</GridItem>
                <GridItem span={9}><b>{ agmt.replicaStatus[0] }</b></GridItem>
                <GridItem span={3}>Replication Enabled:</GridItem>
                <GridItem span={9}><b>{ replEnabled }</b></GridItem>
                <GridItem span={3}>Session In Progress:</GridItem>
                <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                <GridItem span={3}>Consumer:</GridItem>
                <GridItem span={9}><b>{ agmt.replica[0] }</b></GridItem>
                <GridItem span={3}>Changes Sent:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-sent'][0] }</b></GridItem>
                <GridItem span={3}>Changes Skipped:</GridItem>
                <GridItem span={9}><b>{ agmt['number-changes-skipped'][0] }</b></GridItem>
                <GridItem span={3}>Reap Active:</GridItem>
                <GridItem span={9}><b>{ agmt['reap-active'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Init Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Init Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>Last Updated Started:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Ended:</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                <GridItem span={3}>Last Update Status:</GridItem>
                <GridItem span={9}><b>{ agmt['last-update-status'][0] }</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = [...this.state.columns];
        let count = 0;

        for (const replica of this.props.rows) {
            let replEnabled;
            if (!('agmt-name' in replica)) {
                replica['agmt-name'] = ["-"];
            }
            if (!('replica-enabled' in replica)) {
                replica['replica-enabled'] = ["-"];
            }
            if (!('replication-lag-time' in replica)) {
                replica['replication-lag-time'] = ["-"];
            }
            if (!('replication-status' in replica)) {
                replica['replication-status'] = ['-'];
            }
            if (replica['replica-enabled'] == "off") {
                replEnabled =
                    <div className="ds-warning-icon" title="Agreement is disabled">
                        {replica['replica-enabled'][0]} <ExclamationTriangleIcon />
                    </div>;
            } else {
                replEnabled = replica['replica-enabled'][0];
            }
            rows.push(
                {
                    isOpen: false,
                    cells: [
                        replica['agmt-name'][0],
                        { title: replEnabled },
                        replica['replication-status'][0],
                        replica['replication-lag-time'][0],
                    ]
                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(replica) }]
                }
            );
            count += 2;
        }
        if (rows.length == 0) {
            rows = [{ cells: ['No Agreements'] }];
            columns = [{ title: 'Replication Agreements' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onCollapse(event, rowKey, isOpen) {
        const { rows } = this.state;
        // const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[rowKey].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    onSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const agmt of this.props.rows) {
            sorted_agmts.push({
                agmt: agmt,
                1: agmt['agmt-name'][0],
                2: agmt['agmt-enabled'][0],
                3: agmt['replication-status'][0],
                4: agmt['replication-lag-time'][0],
            });
        }

        // Sort the connections and build the new rows
        sorted_agmts.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_agmts.reverse();
        }
        for (let agmt of sorted_agmts) {
            let replEnabled;
            agmt = agmt.agmt;
            if (agmt['replica-enabled'] == "off") {
                replEnabled =
                    <div className="ds-warning-icon" title="Agreement is disabled">
                        {agmt['replica-enabled'][0]} <ExclamationTriangleIcon />
                    </div>;
            } else {
                replEnabled = agmt['replica-enabled'][0];
            }

            rows.push({
                isOpen: false,
                cells: [
                    agmt['agmt-name'][0],
                    { title: replEnabled },
                    agmt['replication-status'][0],
                    agmt['replication-lag-time'][0],
                ]
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [{ title: this.getExpandedRow(agmt) }]
            });
            count += 2;
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: rows,
        });
    }

    render() {
        // This is an expandable list
        const { columns, rows, sortBy } = this.state;
        return (
            <div className="ds-margin-top">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable consumer table"
                    cells={columns}
                    rows={rows}
                    onCollapse={this.onCollapse}
                    variant={TableVariant.compact}
                    sortBy={sortBy}
                    onSort={this.onSort}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}

class ReplDSRCTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortBy: {},
            columns: [
                { title: 'Name', transforms: [sortable] },
                { title: 'Connection Data', transforms: [sortable] },
                { title: 'Bind DN', transforms: [sortable] },
                { title: 'Password', transforms: [sortable] },
                { title: ''},
            ],
            rows: [],
        };
        this.onSort = this.onSort.bind(this);
    }

    componentDidMount() {
        let columns = [...this.state.columns];
        let rows = [];

        for (const conn of this.props.rows) {
            let cred = conn[4];
            if (conn[4] === "*") {
                const desc = <i>Prompt</i>;
                cred = { title: desc };
            } else if (!conn[4].startsWith("[")) {
                cred = "**********";
            }
            rows.push({
                cells: [
                    conn[0], conn[1] + ":" + conn[2], conn[3], cred, { props: { textCenter: true }, title: this.props.getDeleteButton(conn[0]) }
                ]
            });
        }
        if (this.props.rows.length == 0) {
            rows = [{ cells: ['There is no saved replication monitor connections'] }];
            columns = [{ title: 'Replication Connections' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [...this.state.rows];
        const sortedRows = rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    render() {
        const { columns, rows, sortBy } = this.state;

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Sortable DSRC Table"
                    sortBy={sortBy}
                    onSort={this.onSort}
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}

class ReplDSRCAliasTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            sortBy: {},
            columns: [
                { title: 'Alias', transforms: [sortable] },
                { title: 'Connection Data', transforms: [sortable] },
                { title: ''}
            ],
            rows: [],
        };
        this.onSort = this.onSort.bind(this);
    }

    componentDidMount() {
        let columns = [...this.state.columns];
        let rows = [];

        for (const alias of this.props.rows) {
            rows.push({
                cells: [
                    alias[0], alias[1] + ":" + alias[2], { props: { textCenter: true }, title: this.props.getDeleteButton(alias[0]) }
                ]
            });
        }
        if (this.props.rows.length == 0) {
            rows = [{ cells: ['There are no saved replication monitor aliases'] }];
            columns = [{ title: 'Replication Monitoring Aliases' }];
        }
        this.setState({
            rows: rows,
            columns: columns
        });
    }

    onSort(_event, index, direction) {
        let rows = [...this.state.rows];
        const sortedRows = rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    render() {
        const { columns, rows, sortBy } = this.state;

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Sortable DSRC Table"
                    sortBy={sortBy}
                    onSort={this.onSort}
                    cells={columns}
                    rows={rows}
                    variant={TableVariant.compact}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );
    }
}


// Proptypes and defaults
ReplDSRCAliasTable.defaultProps = {
    rows: PropTypes.array
};

ReplDSRCAliasTable.defaultProps = {
    rows: []
};

ReplDSRCTable.defaultProps = {
    rows: PropTypes.array
};

ReplDSRCTable.defaultProps = {
    rows: []
};

AgmtTable.propTypes = {
    agmts: PropTypes.array,
    pokeAgmt: PropTypes.func,
};

AgmtTable.defaultProps = {
    agmts: [],
};

WinsyncAgmtTable.propTypes = {
    agmts: PropTypes.array,
    pokeAgmt: PropTypes.func,
};

WinsyncAgmtTable.defaultProps = {
    agmts: [],
};

ConnectionTable.propTypes = {
    conns: PropTypes.array,
};

ConnectionTable.defaultProps = {
    conns: [],
};

CleanALLRUVTable.propTypes = {
    tasks: PropTypes.array,
};

CleanALLRUVTable.defaultProps = {
    tasks: [],
};

AbortCleanALLRUVTable.propTypes = {
    tasks: PropTypes.array,
};

AbortCleanALLRUVTable.defaultProps = {
    tasks: [],
};

ConflictTable.propTypes = {
    conflicts: PropTypes.array,
    resolveConflict: PropTypes.func,
};

ConflictTable.defaultProps = {
    conflicts: [],
};

GlueTable.propTypes = {
    glues: PropTypes.array,
    convertGlue: PropTypes.func,
    deleteGlue: PropTypes.func,
};

GlueTable.defaultProps = {
    glues: PropTypes.array,
};

ReportCredentialsTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

ReportCredentialsTable.defaultProps = {
    rows: [],
};

ReportAliasesTable.propTypes = {
    rows: PropTypes.array,
    editConfig: PropTypes.func,
    deleteConfig: PropTypes.func
};

ReportAliasesTable.defaultProps = {
    rows: [],
};

ReportConsumersTable.propTypes = {
    rows: PropTypes.array,
};

ReportConsumersTable.defaultProps = {
    rows: [],
};

ReportSingleTable.propTypes = {
    rows: PropTypes.array,
};

ReportSingleTable.defaultProps = {
    rows: [],
};

DiskTable.defaultProps = {
    rows: PropTypes.array
};

DiskTable.defaultProps = {
    rows: []
};

export {
    ConnectionTable,
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
    WinsyncAgmtTable,
    AgmtTable,
    ConflictTable,
    GlueTable,
    ReportCredentialsTable,
    ReportAliasesTable,
    ReportConsumersTable,
    ReportSingleTable,
    DiskTable,
    ReplDSRCTable,
    ReplDSRCAliasTable,
};
