import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Grid,
    GridItem,
    Pagination,
    SearchInput,
    Text,
    TextContent,
    TextVariants,
    EmptyState,
    EmptyStateBody,
    EmptyStateIcon,
    Title,
} from '@patternfly/react-core';
import {
    SortByDirection,
    Table,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
    ExpandableRowContent,
    ActionsColumn,
    sortable
} from '@patternfly/react-table';
import {
    CheckIcon,
    MinusIcon,
    SearchIcon,
} from '@patternfly/react-icons';
import { ExclamationTriangleIcon } from '@patternfly/react-icons/dist/js/icons/exclamation-triangle-icon';
import PropTypes from "prop-types";
import { get_date_string, numToCommas } from "../tools.jsx";
import { LagReportModal } from "./monitorModals.jsx";

const _ = cockpit.gettext;

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
                { title: _("Task"), sortable: true },
                { title: _("Created"), sortable: true },
                { title: _("Replica ID"), sortable: true },
                { title: _("Status"), sortable: true },
                { title: _("Actions")}
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.getLog = this.getLog.bind(this);
        this.createRows = this.createRows.bind(this);
    }

    getLog(log) {
        return (
            <TextContent>
                <Text
                    component={TextVariants.pre}
                    style={{
                        whiteSpace: 'pre-wrap',
                        wordBreak: 'break-word'
                    }}
                >
                    {log}
                </Text>
            </TextContent>
        );
    }

    createRows(tasks) {
        let rows = [];
        let columns = [...this.state.columns];

        for (const task of tasks) {
            rows.push({
                isOpen: false,
                cells: [
                    task.attrs.cn[0],
                    get_date_string(task.attrs.nstaskcreated[0]),
                    task.attrs['replica-id'][0],
                    task.attrs.nstaskstatus[0],
                ],
                originalData: task
            });
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Tasks")] }];
            columns = [{ title: _("Abort CleanAllRUV Tasks") }];
        }

        return { rows, columns };
    }

    componentDidMount() {
        const { rows, columns } = this.createRows(this.props.tasks);
        this.setState({ rows, columns });
    }

    componentDidUpdate(prevProps) {
        if (prevProps.tasks !== this.props.tasks) {
            const { rows, columns } = this.createRows(this.props.tasks);
            this.setState({
                rows,
                columns,
                page: 1
            });
        }
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        rows[rowIndex].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sorted_tasks = [];
        const rows = [];

        for (const task of this.props.tasks) {
            sorted_tasks.push({
                task,
                1: task.attrs.cn[0],
                2: get_date_string(task.attrs.nstaskcreated[0]),
                3: task.attrs['replica-id'][0],
                4: task.attrs.nstaskstatus[0]
            });
        }

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
                ],
                originalData: task
            });
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);
        const hasNoTasks = rows.length === 1 && rows[0].cells.length === 1;

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Expandable table"
                    variant='compact'
                >
                    <Thead>
                        <Tr>
                            {!hasNoTasks && <Th screenReaderText="Row expansion" />}
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {!hasNoTasks && (
                                        <Td
                                            expand={{
                                                rowIndex,
                                                isExpanded: row.isOpen,
                                                onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                            }}
                                        />
                                    )}
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))}
                                </Tr>
                                {row.isOpen && row.originalData && (
                                    <Tr isExpanded={true}>
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            <ExpandableRowContent>
                                                {this.getLog(row.originalData.attrs.nstasklog[0])}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.tasks.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Task"), sortable: true },
                { title: _("Created"), sortable: true },
                { title: _("Replica ID"), sortable: true },
                { title: _("Status"), sortable: true },
                { title: _("Actions")}
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.getLog = this.getLog.bind(this);
        this.createRows = this.createRows.bind(this);
    }

    getLog(log) {
        return (
            <TextContent>
                <Text
                    component={TextVariants.pre}
                    style={{
                        whiteSpace: 'pre-wrap',
                        wordBreak: 'break-word'
                    }}
                >
                    {log}
                </Text>
            </TextContent>
        );
    }

    createRows(tasks) {
        let rows = [];
        let columns = [...this.state.columns];

        for (const task of tasks) {
            rows.push({
                isOpen: false,
                cells: [
                    task.attrs.cn[0],
                    get_date_string(task.attrs.nstaskcreated[0]),
                    task.attrs['replica-id'][0],
                    task.attrs.nstaskstatus[0],
                ],
                originalData: task
            });
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Tasks")] }];
            columns = [{ title: _("CleanAllRUV Tasks") }];
        }

        return { rows, columns };
    }

    componentDidMount() {
        const { rows, columns } = this.createRows(this.props.tasks);
        this.setState({ rows, columns });
    }

    componentDidUpdate(prevProps) {
        if (prevProps.tasks !== this.props.tasks) {
            const { rows, columns } = this.createRows(this.props.tasks);
            this.setState({
                rows,
                columns,
                page: 1 // Reset to first page when data changes
            });
        }
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        rows[rowIndex].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sorted_tasks = [];
        const rows = [];

        for (const task of this.props.tasks) {
            sorted_tasks.push({
                task,
                1: task.attrs.cn[0],
                2: get_date_string(task.attrs.nstaskcreated[0]),
                3: task.attrs['replica-id'][0],
                4: task.attrs.nstaskstatus[0]
            });
        }

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
                ],
                originalData: task
            });
        }
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);
        const hasNoTasks = rows.length === 1 && rows[0].cells.length === 1;

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Expandable table"
                    variant='compact'
                >
                    <Thead>
                        <Tr>
                            {!hasNoTasks && <Th screenReaderText="Row expansion" />}
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {!hasNoTasks && (
                                        <Td
                                            expand={{
                                                rowIndex,
                                                isExpanded: row.isOpen,
                                                onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                            }}
                                        />
                                    )}
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))}
                                </Tr>
                                {row.isOpen && row.originalData && (
                                    <Tr isExpanded={true}>
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            <ExpandableRowContent>
                                                {this.getLog(row.originalData.attrs.nstasklog[0])}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.tasks.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Agreement"), sortable: true },
                { title: _("Replica"), sortable: true },
                { title: _("Enabled"), sortable: true },
                { title: _("Poke"), sortable: false },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        return (
            <ExpandableRowContent>
                <Grid className="ds-indent">
                    <GridItem span={3}>{_("Session In Progress:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                    <GridItem span={3}>{_("Changes Sent:")}</GridItem>
                    <GridItem span={9}><b>{ numToCommas(agmt['number-changes-sent'][0]) }</b></GridItem>
                    <hr />
                    <GridItem span={3}>{_("Last Init Started:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Init Ended:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Init Status:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                    <hr />
                    <GridItem span={3}>{_("Last Updated Started:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Update Ended:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Update Status:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['last-update-status'][0] }</b></GridItem>
                </Grid>
            </ExpandableRowContent>
        );
    }

    getWakeupButton(name) {
        return (
            <Button
                id={name}
                variant="primary"
                onClick={this.props.handlePokeAgmt}
                title={_("Awaken the winsync replication agreement")}
                size="sm"
            >
                {_("Poke")}
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
                ],
                originalData: agmt
            });
            count += 1;
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Agreements")] }];
            columns = [{ title: _("Winsync Agreements") }];
        }

        this.setState({
            rows,
            columns
        });
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        rows[rowIndex].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];

        for (const agmt of this.props.agmts) {
            sorted_agmts.push({
                agmt,
                1: agmt['agmt-name'][0],
                2: agmt.replica[0],
                3: agmt['replica-enabled'][0],
            });
        }

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
                ],
                originalData: agmt
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);
        const hasNoAgreements = rows.length === 1 && rows[0].cells.length === 1;

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Winsync agreements table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {!hasNoAgreements && <Th screenReaderText="Row expansion" />}
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {!hasNoAgreements && (
                                        <Td
                                            expand={{
                                                rowIndex,
                                                isExpanded: row.isOpen,
                                                onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                            }}
                                        />
                                    )}
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell.title || cell}</Td>
                                    ))}
                                </Tr>
                                {row.isOpen && row.originalData && (
                                    <Tr isExpanded={true}>
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            {this.getExpandedRow(row.originalData)}
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.agmts.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Agreement"), sortable: true },
                { title: _("Replica"), sortable: true },
                { title: _("Enabled"), sortable: true },
                { title: '', sortable: false, screenReaderText: _("Poke the agreement") },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        return (
            <ExpandableRowContent>
                <Grid className="ds-indent">
                    <GridItem span={3}>{_("Session In Progress:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                    <GridItem span={3}>{_("Changes Sent:")}</GridItem>
                    <GridItem span={9}><b>{ numToCommas(agmt['number-changes-sent'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Changes Skipped:")}</GridItem>
                    <GridItem span={9}><b>{ numToCommas(agmt['number-changes-skipped'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Reap Active:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['reap-active'][0] }</b></GridItem>
                    <hr />
                    <GridItem span={3}>{_("Last Init Started:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Init Ended:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Init Status:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                    <hr />
                    <GridItem span={3}>{_("Last Updated Started:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Update Ended:")}</GridItem>
                    <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                    <GridItem span={3}>{_("Last Update Status:")}</GridItem>
                    <GridItem span={9}><b>{ agmt['last-update-status'][0] }</b></GridItem>
                </Grid>
            </ExpandableRowContent>
        );
    }

    getWakeupButton(name) {
        return (
            <Button
                id={name}
                variant="primary"
                onClick={this.props.handlePokeAgmt}
                title={_("Awaken the replication agreement")}
                size="sm"
            >
                {_("Poke")}
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
                ],
                originalData: agmt
            });
            count += 1;
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Agreements")] }];
            columns = [{ title: _("Replication Agreements") }];
        }

        this.setState({
            rows,
            columns
        });
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        rows[rowIndex].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];

        for (const agmt of this.props.agmts) {
            sorted_agmts.push({
                agmt,
                1: agmt['agmt-name'][0],
                2: agmt.replica[0],
                3: agmt['replica-enabled'][0],
            });
        }

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
                ],
                originalData: agmt
            });
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);
        const hasNoAgreements = rows.length === 1 && rows[0].cells.length === 1;

        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Agreements table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {!hasNoAgreements && <Th screenReaderText="Row expansion" />}
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                    screenReaderText={column.screenReaderText}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {!hasNoAgreements && (
                                        <Td
                                            expand={{
                                                rowIndex,
                                                isExpanded: row.isOpen,
                                                onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                            }}
                                        />
                                    )}
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell.title || cell}</Td>
                                    ))}
                                </Tr>
                                {row.isOpen && row.originalData && (
                                    <Tr isExpanded={true}>
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            {this.getExpandedRow(row.originalData)}
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.agmts.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                    title: _("Connection Opened"),
                    sortable: true
                },
                { title: _("IP Address"), sortable: true },
                { title: _("Conn ID"), sortable: true },
                { title: _("Bind DN"), sortable: true },
                {
                    title: _("Max Threads"),
                    sortable: true,
                    info: {
                        tooltip: _("If connection is currently at \"Max Threads\" then it will block new operations")
                    }
                },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(ip, conn_date, parts) {
        return (
            <Grid className="ds-indent ds-margin-top ds-margin-bottom">
                <GridItem span={3}>{_("IP Address:")}</GridItem>
                <GridItem span={4}><b>{ip}</b></GridItem>
                <GridItem span={3}>{_("File Descriptor:")}</GridItem>
                <GridItem span={2}><b>{parts[0]}</b></GridItem>
                <GridItem span={3}>{_("Connection Opened:")}</GridItem>
                <GridItem span={4}><b>{conn_date}</b></GridItem>
                <GridItem span={3}>{_("Operations Started:")}</GridItem>
                <GridItem span={2}><b>{numToCommas(parts[2])}</b></GridItem>
                <GridItem span={3}>{_("Connection ID:")}</GridItem>
                <GridItem span={4}><b>{parts[9]}</b></GridItem>
                <GridItem span={3}>{_("Operations Finished:")}</GridItem>
                <GridItem span={2}><b>{numToCommas(parts[3])}</b></GridItem>
                <GridItem span={3}>{_("Bind DN:")}</GridItem>
                <GridItem span={4}><b>{parts[5]}</b></GridItem>
                <GridItem span={3}>{_("Read/write Blocked:")}</GridItem>
                <GridItem span={2}><b>{numToCommas(parts[4])}</b></GridItem>
                <GridItem className="ds-margin-top-lg" span={5}>{_("Connection Currently At Max Threads:")}</GridItem>
                <GridItem className="ds-margin-top-lg" span={7}><b>{parts[6] === "1" ? _("Yes") : _("No")}</b></GridItem>
                <GridItem span={5}>{_("Number Of Times Connection Hit Max Threads:")}</GridItem>
                <GridItem span={7}><b>{numToCommas(parts[7])}</b></GridItem>
                <GridItem span={5}>{_("Number Of Operations Blocked By Max Threads:")}</GridItem>
                <GridItem span={7}><b>{numToCommas(parts[8])}</b></GridItem>
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
            if (ip === "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);
            rows.push({
                isOpen: false,
                cells: [
                    conn_date, ip, parts[9], parts[5], parts[6] === "1" ? "Yes" : "No"
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
            rows
        });
    }

    handleCollapse(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey;
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;
        for (const conn of this.props.conns) {
            const ip_parts = conn.split(':ip=');
            const parts = conn.split(':', 10);
            // Process the IP address
            let ip = ip_parts[1];
            if (ip === "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);
            const val = value.toLowerCase();
            const conn_raw = conn.toLowerCase();
            // Check for matches of all the parts
            if (val !== "" && conn_raw.indexOf(val) === -1 &&
                ip.toLowerCase().indexOf(val) === -1 &&
                conn_date.indexOf(value) === -1) {
                // Not a match
                continue;
            }
            rows.push({
                isOpen: false,
                cells: [
                    conn_date, ip, parts[9], parts[5], parts[6] === "1" ? "Yes" : "No"
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
            rows,
            value,
            page: 1,
        });
    }

    handleSort(_event, index, direction) {
        const sorted_conns = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const conn of this.props.conns) {
            const ip_parts = conn.split(':ip=');
            const parts = conn.split(':', 10);
            let ip = ip_parts[1];
            if (ip === "local") {
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
            if (ip === "local") {
                ip = "LDAPI";
            }
            const conn_date = get_date_string(parts[1]);
            rows.push({
                isOpen: false,
                cells: [
                    conn_date, ip, parts[9], parts[5], parts[6] === "1" ? "Yes" : "No"
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
            rows,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = origRows.splice(startIdx, perPage * 2);
        for (let idx = 1, count = 0; idx < tableRows.length; idx += 2, count += 2) {
            tableRows[idx].parent = count;
        }

        return (
            <div className="ds-margin-top-xlg">
                <TextContent>
                    <Text component={TextVariants.h4}>
                        {_("Client Connections:")}<b className="ds-left-margin">{numToCommas(this.props.conns.length)}</b>
                    </Text>
                </TextContent>
                <SearchInput
                    className="ds-margin-top-xlg"
                    placeholder={_("Search connections")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table
                    aria-label="Expandable table"
                    variant='compact'
                >
                    <Thead>
                        <Tr>
                            <Th screenReaderText="Row expansion" />
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: columnIndex + 1
                                    } : undefined}
                                    info={column.info}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => {
                            if (row.parent !== undefined) {
                                // This is an expanded row
                                return (
                                    <Tr
                                        key={rowIndex}
                                        isExpanded={tableRows[row.parent].isOpen}
                                    >
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            <ExpandableRowContent>
                                                {/* Render the expanded content directly */}
                                                {row.cells[0].title}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                );
                            }
                            // This is a regular row
                            return (
                                <Tr key={rowIndex}>
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                        }}
                                    />
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {/* Ensure we're rendering a string or valid React element */}
                                            {typeof cell === 'object' ? cell.title : cell}
                                        </Td>
                                    ))}
                                </Tr>
                            );
                        })}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.conns.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
            columns: [
                { title: _("Glue Entry"), sortable: true },
                { title: _("Description"), sortable: true },
                { title: _("Created"), sortable: true },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage,
                page: 1
            });
        };

        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        const rows = [];
        for (const glue of this.props.glues) {
            rows.push([
                glue.dn,
                glue.attrs.nsds5replconflict[0],
                get_date_string(glue.attrs.createtimestamp[0])
            ]);
        }
        this.setState({ rows });
    }

    handleSort(_event, index, direction) {
        const sortedGlues = [...this.state.rows];

        sortedGlues.sort((a, b) => {
            const aValue = a[index];
            const bValue = b[index];
            return aValue < bValue ? -1 : aValue > bValue ? 1 : 0;
        });

        if (direction !== SortByDirection.asc) {
            sortedGlues.reverse();
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: sortedGlues,
            page: 1,
        });
    }

    getActions(rowData) {
        return [
            {
                title: _("Convert Glue Entry"),
                onClick: () => this.props.convertGlue(rowData[0])
            },
            {
                title: _("Delete Glue Entry"),
                onClick: () => this.props.deleteGlue(rowData[0])
            }
        ];
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;
        const hasRows = this.props.glues.length > 0;

        // Calculate pagination
        const startIdx = (perPage * page) - perPage;
        let tableRows = [...rows].splice(startIdx, perPage);
        let displayColumns = [...columns];

        if (!hasRows) {
            tableRows = [[_("No Glue Entries")]];
            displayColumns = [{ title: _("Replication Conflict Glue Entries") }];
        }

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="glue table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {displayColumns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={hasRows && column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>{cell}</Td>
                                ))}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn
                                            items={this.getActions(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={this.props.glues.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Conflict DN"), sortable: true },
                { title: _("Description"), sortable: true },
                { title: _("Created"), sortable: true },
                { title: '', sortable: false, screenReaderText: _("Resolve the conflict") },
            ],
        };

        this.handleSetPage = (_event, pageNumber) => {
            this.setState({
                page: pageNumber
            });
        };

        this.handlePerPageSelect = (_event, perPage) => {
            this.setState({
                perPage
            });
        };

        this.handleSort = this.handleSort.bind(this);
        this.getResolveButton = this.getResolveButton.bind(this);
    }

    getResolveButton(name) {
        return (
            <Button
                id={name}
                variant="primary"
                size="sm"
                onClick={() => {
                    this.props.resolveConflict(name);
                }}
            >
                {_("Resolve")}
            </Button>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        for (const conflict of this.props.conflicts) {
            rows.push([
                conflict.dn,
                conflict.attrs.nsds5replconflict[0],
                get_date_string(conflict.attrs.createtimestamp[0]),
                this.getResolveButton(conflict.dn)
            ]);
        }
        if (rows.length === 0) {
            rows = [[_("No Conflict Entries")]];
            columns = [{ title: _("Replication Conflict Entries") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, index, direction) {
        const sortedConflicts = [...this.state.rows];

        sortedConflicts.sort((a, b) => {
            const aValue = a[index];
            const bValue = b[index];
            if (typeof aValue === 'string') {
                return aValue.localeCompare(bValue);
            }
            return 0;
        });

        if (direction !== SortByDirection.asc) {
            sortedConflicts.reverse();
        }

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: sortedConflicts,
            page: 1,
        });
    }

    render() {
        const { columns, rows, perPage, page, sortBy } = this.state;

        // Calculate pagination
        const startIdx = (perPage * page) - perPage;
        const tableRows = [...rows].splice(startIdx, perPage);

        return (
            <div className="ds-margin-top-lg">
                <Table
                    className="ds-margin-top"
                    aria-label="conflict table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                    screenReaderText={column.screenReaderText}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>
                                        {cell}
                                    </Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                <Pagination
                    itemCount={rows.length}
                    widgetId="pagination-options-menu-bottom"
                    perPage={perPage}
                    page={page}
                    variant="bottom"
                    onSetPage={this.handleSetPage}
                    onPerPageSelect={this.handlePerPageSelect}
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
                { title: _("Disk Partition"), sortable: true },
                { title: _("Disk Size"), sortable: true },
                { title: _("Used Space"), sortable: true },
                { title: _("Available Space"), sortable: true },
            ],
        };
        this.handleSort = this.handleSort.bind(this);
    }

    handleSort(_event, columnIndex, direction) {
        const sortedRows = [...this.props.rows].sort((a, b) => (
            a[columnIndex] < b[columnIndex] ? -1 : a[columnIndex] > b[columnIndex] ? 1 : 0
        ));

        this.setState({
            sortBy: {
                index: columnIndex,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    render() {
        const { columns, sortBy } = this.state;

        return (
            <div className="ds-margin-top-xlg">
                <Table aria-label="Sortable Table" variant="compact">
                    <Thead>
                        <Tr>
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {this.props.rows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>{cell}</Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
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
            columns: [
                { title: _("Alias"), sortable: true },
                { title: _("Connection Data"), sortable: true },
            ],
        };
    }

    getActions(rowData) {
        return [
            {
                title: _("Edit Alias"),
                onClick: () => this.props.editConfig(rowData[0], rowData[1])
            },
            {
                title: _("Delete Alias"),
                onClick: () => this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        let columns = this.state.columns;
        let rows = JSON.parse(JSON.stringify(this.props.rows)); // Deep copy
        const hasRows = rows.length > 0;

        if (!hasRows) {
            rows = [[_("No Aliases")]];
            columns = [{ title: _("Instance Aliases") }];
        }

        return (
            <div className="ds-margin-top-xlg">
                <TextContent>
                    <Text className="ds-center ds-margin-bottom" component="h4">
                        {_("Replica Naming Aliases")}
                    </Text>
                </TextContent>
                <Table
                    variant="compact"
                    aria-label="Sortable Table"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={hasRows && column.sortable ? {
                                        sortBy: this.props.sortBy,
                                        onSort: this.props.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {rows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ?
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                    :
                                    <Td>{row.cells[0]}</Td>
                                }
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn
                                            items={this.getActions(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
            </div>
        );
    }
}

class ReportCredentialsTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            columns: [
                { title: _("Connection Data"), sortable: true },
                { title: _("Bind DN"), sortable: true },
                { title: _("Password"), sortable: true },
            ],
        };
    }

    getActions(rowData) {
        return [
            {
                title: _("Edit Connection"),
                onClick: () => this.props.editConfig(rowData[0], rowData[1], rowData.credsBindpw, rowData.pwInteractive)
            },
            {
                title: _("Delete Connection"),
                onClick: () => this.props.deleteConfig(rowData[0])
            }
        ];
    }

    render() {
        const { columns } = this.state;
        let tableRows = [];
        let displayColumns = [...columns];
        const hasRows = this.props.rows.length > 0;

        if (!hasRows) {
            tableRows = [[_("No Credentials")]];
            displayColumns = [{ title: _("Credentials Table") }];
        } else {
            tableRows = this.props.rows.map(row => {
                const rowCopy = JSON.parse(JSON.stringify(row)); // Deep copy
                const pwInteractive = rowCopy.pwInputInterractive;
                let pwField = <i>{_("Interactive Input is set")}</i>;

                if (!pwInteractive) {
                    if (rowCopy.credsBindpw === "") {
                        pwField = <i>{_("Both Password or Interactive Input flag are not set")}</i>;
                    } else {
                        pwField = "********";
                    }
                }

                const cells = [
                    rowCopy.connData,
                    rowCopy.credsBinddn,
                    pwField
                ];
                cells.credsBindpw = rowCopy.credsBindpw;
                cells.pwInteractive = pwInteractive;
                return cells;
            });
        }

        return (
            <div className="ds-margin-top">
                <TextContent>
                    <Text className="ds-center ds-margin-bottom" component={TextVariants.h4}>
                        {_("Replication Report Credentials")}
                    </Text>
                </TextContent>
                <Table
                    aria-label="Cred Table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {displayColumns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={hasRows && column.sortable ? {
                                        sortBy: this.props.sortBy,
                                        onSort: this.props.handleSort,
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {Array.isArray(row) ?
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                    :
                                    <Td>{row}</Td>
                                }
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn
                                            items={this.getActions(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
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
                {
                    title: _("Supplier"),
                    sortable: true
                },
                { title: _("Agreement"), sortable: true },
                { title: _("Status"), sortable: true },
                { title: _("Lag"), sortable: true },
            ],
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        if (agmt['agmt-name'][0] === "-") {
            return (
                <TextContent>
                    <Text component={TextVariants.h4}>
                        {_("No agreement information")}
                    </Text>
                </TextContent>
            );
        }
        let replEnabled;
        if (agmt['replica-enabled'] === "off") {
            replEnabled = (
                <div className="ds-warning-icon">
                    {agmt['replica-enabled'][0]} <ExclamationTriangleIcon />
                </div>
            );
        } else {
            replEnabled = agmt['replica-enabled'][0];
        }
        return (
            <Grid className="ds-indent">
                <GridItem span={3}>{_("Suffix & Replica ID:")}</GridItem>
                <GridItem span={9}><b>{ agmt.replicaName[0] }</b></GridItem>
                <GridItem span={3}>{_("Replica Server Status:")}</GridItem>
                <GridItem span={9}><b>{ agmt.replicaStatus[0] }</b></GridItem>
                <GridItem span={3}>{_("Replication Enabled:")}</GridItem>
                <GridItem span={9}><b>{ replEnabled }</b></GridItem>
                <GridItem span={3}>{_("Session In Progress:")}</GridItem>
                <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                <GridItem span={3}>{_("Consumer:")}</GridItem>
                <GridItem span={9}><b>{ agmt.replica[0] }</b></GridItem>
                <GridItem span={3}>{_("Changes Sent:")}</GridItem>
                <GridItem span={9}><b>{ numToCommas(agmt['number-changes-sent'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Changes Skipped:")}</GridItem>
                <GridItem span={9}><b>{ numToCommas(agmt['number-changes-skipped'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Reap Active:")}</GridItem>
                <GridItem span={9}><b>{ agmt['reap-active'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>{_("Last Init Started:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Init Ended:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Init Status:")}</GridItem>
                <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>{_("Last Updated Started:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Update Ended:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Update Status:")}</GridItem>
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
            if (replica['replica-enabled'][0] === "off") {
                agmtName = (
                    <div className="ds-warning-icon" title={_("Agreement is disabled")}>
                        {replica['agmt-name'][0]} <ExclamationTriangleIcon />
                    </div>
                );
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
        if (rows.length === 0) {
            rows = [{ cells: [_("No Agreements")] }];
            columns = [{ title: _("Replication Agreements") }];
        }

        this.setState({
            rows,
            columns
        });
    }

    handleCollapse(event, rowKey, isOpen) {
        const { rows } = this.state;
        rows[rowKey].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const agmt of this.props.rows) {
            sorted_agmts.push({
                agmt,
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
            if (agmt['replica-enabled'] === "off") {
                agmtName = (
                    <div className="ds-warning-icon" title={_("Agreement is disabled")}>
                        {agmt['agmt-name'][0]} <ExclamationTriangleIcon />
                    </div>
                );
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
            rows,
        });
    }

    render() {
        const { columns, rows, sortBy } = this.state;
        return (
            <div className="ds-margin-top-xlg">
                <Table
                    aria-label="Expandable table"
                    variant='compact'
                >
                    <Thead>
                        <Tr>
                            <Th screenReaderText="Row expansion" />
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: columnIndex + 1
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {rows.map((row, rowIndex) => {
                            if (row.parent !== undefined) {
                                // Expanded row
                                return (
                                    <Tr
                                        key={rowIndex}
                                        isExpanded={rows[row.parent].isOpen}
                                    >
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            <ExpandableRowContent>
                                                {row.cells[0].title}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                );
                            }
                            // Regular row
                            return (
                                <Tr key={rowIndex}>
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                        }}
                                    />
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {typeof cell === 'object' ? cell.title : cell}
                                        </Td>
                                    ))}
                                </Tr>
                            );
                        })}
                    </Tbody>
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
                { title: _("Agreement Name"), sortable: true },
                { title: _("Enabled"), sortable: true },
                { title: _("Status"), sortable: true },
                { title: _("Lag"), sortable: true },
            ],
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.getExpandedRow = this.getExpandedRow.bind(this);
    }

    getExpandedRow(agmt) {
        let replEnabled;
        if (agmt['agmt-name'][0] === "-") {
            return (
                <TextContent>
                    <Text component={TextVariants.h4}>
                        {_("No agreement information")}
                    </Text>
                </TextContent>
            );
        }
        if (agmt['replica-enabled'] === "off") {
            replEnabled = (
                <div className="ds-warning-icon">
                    {agmt['replica-enabled'][0]} <ExclamationTriangleIcon />
                </div>
            );
        } else {
            replEnabled = agmt['replica-enabled'][0];
        }
        return (
            <Grid className="ds-margin-left">
                <GridItem span={3}>{_("Suffix & Replica ID:")}</GridItem>
                <GridItem span={9}><b>{ agmt.replicaName[0] }</b></GridItem>
                <GridItem span={3}>{_("Replica Server Status:")}</GridItem>
                <GridItem span={9}><b>{ agmt.replicaStatus[0] }</b></GridItem>
                <GridItem span={3}>{_("Replication Enabled:")}</GridItem>
                <GridItem span={9}><b>{ replEnabled }</b></GridItem>
                <GridItem span={3}>{_("Session In Progress:")}</GridItem>
                <GridItem span={9}><b>{ agmt['update-in-progress'][0] }</b></GridItem>
                <GridItem span={3}>{_("Consumer:")}</GridItem>
                <GridItem span={9}><b>{ agmt.replica[0] }</b></GridItem>
                <GridItem span={3}>{_("Changes Sent:")}</GridItem>
                <GridItem span={9}><b>{ numToCommas(agmt['number-changes-sent'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Changes Skipped:")}</GridItem>
                <GridItem span={9}><b>{ numToCommas(agmt['number-changes-skipped'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Reap Active:")}</GridItem>
                <GridItem span={9}><b>{ agmt['reap-active'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>{_("Last Init Started:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-start'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Init Ended:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-init-end'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Init Status:")}</GridItem>
                <GridItem span={9}><b>{ agmt['last-init-status'][0] }</b></GridItem>
                <hr />
                <GridItem span={3}>{_("Last Updated Started:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-start'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Update Ended:")}</GridItem>
                <GridItem span={9}><b>{ get_date_string(agmt['last-update-end'][0]) }</b></GridItem>
                <GridItem span={3}>{_("Last Update Status:")}</GridItem>
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
            if (replica['replica-enabled'] === "off") {
                replEnabled = (
                    <div className="ds-warning-icon" title={_("Agreement is disabled")}>
                        {replica['replica-enabled'][0]} <ExclamationTriangleIcon />
                    </div>
                );
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
        if (rows.length === 0) {
            rows = [{ cells: [_("No Agreements")] }];
            columns = [{ title: _("Replication Agreements") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleCollapse(event, rowKey, isOpen) {
        const { rows } = this.state;
        rows[rowKey].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSort(_event, index, direction) {
        const sorted_agmts = [];
        const rows = [];
        let count = 0;

        // Convert the conns into a sortable array based on the column indexes
        for (const agmt of this.props.rows) {
            sorted_agmts.push({
                agmt,
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
            if (agmt['replica-enabled'] === "off") {
                replEnabled = (
                    <div className="ds-warning-icon" title={_("Agreement is disabled")}>
                        {agmt['replica-enabled'][0]} <ExclamationTriangleIcon />
                    </div>
                );
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
            rows,
        });
    }

    render() {
        const { columns, rows, sortBy } = this.state;
        return (
            <div className="ds-margin-top">
                <Table
                    className="ds-margin-top"
                    aria-label="Expandable consumer table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            <Th screenReaderText="Row expansion" />
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: columnIndex + 1
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {rows.map((row, rowIndex) => {
                            if (row.parent !== undefined) {
                                return (
                                    <Tr
                                        key={rowIndex}
                                        isExpanded={rows[row.parent].isOpen}
                                    >
                                        <Td />
                                        <Td
                                            colSpan={columns.length + 1}
                                            noPadding
                                        >
                                            <ExpandableRowContent>
                                                {row.cells[0].title}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                );
                            }
                            return (
                                <Tr key={rowIndex}>
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                        }}
                                    />
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {typeof cell === 'object' ? cell.title : cell}
                                        </Td>
                                    ))}
                                </Tr>
                            );
                        })}
                    </Tbody>
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
                { title: _("Name"), sortable: true },
                { title: _("Connection Data"), sortable: true },
                { title: _("Bind DN"), sortable: true },
                { title: _("Password"), sortable: true },
                { title: '', sortable: false, screenReaderText: _("Delete Button") }
            ],
            rows: [],
        };
        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        let columns = [...this.state.columns];
        let rows = [];

        for (const conn of this.props.rows) {
            let cred = conn[4];
            if (conn[4] === "*") {
                const desc = <i>Prompt</i>;
                cred = desc;
            } else if (!conn[4].startsWith("[")) {
                cred = "**********";
            }
            rows.push([
                conn[0],
                `${conn[1]}:${conn[2]}`,
                conn[3],
                cred,
                <div className="pf-v5-u-text-align-center">{this.props.getDeleteButton(conn[0])}</div>
            ]);
        }
        if (this.props.rows.length === 0) {
            rows = [[_("There is no saved replication monitor connections")]];
            columns = [{ title: _("Replication Connections") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, columnIndex, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => (
            a[columnIndex] < b[columnIndex] ? -1 : a[columnIndex] > b[columnIndex] ? 1 : 0
        ));

        this.setState({
            sortBy: {
                index: columnIndex,
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
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                    screenReaderText={column.screenReaderText}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {rows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>{cell}</Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
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
                { title: _("Alias"), sortable: true },
                { title: _("Connection Data"), sortable: true },
                { title: '', sortable: false, screenReaderText: _("Delete Button") }
            ],
            rows: [],
        };
        this.handleSort = this.handleSort.bind(this);
    }

    componentDidMount() {
        let columns = [...this.state.columns];
        let rows = [];

        for (const alias of this.props.rows) {
            rows.push([
                alias[0],
                alias[1] + ":" + alias[2],
                <div className="pf-v5-u-text-align-center">{this.props.getDeleteButton(alias[0])}</div>
            ]);
        }
        if (this.props.rows.length === 0) {
            rows = [[_("There are no saved replication monitor aliases")]];
            columns = [{ title: _("Replication Monitoring Aliases") }];
        }
        this.setState({
            rows,
            columns
        });
    }

    handleSort(_event, columnIndex, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) => (
            a[columnIndex] < b[columnIndex] ? -1 : a[columnIndex] > b[columnIndex] ? 1 : 0
        ));

        this.setState({
            sortBy: {
                index: columnIndex,
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
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {columns.map((column, columnIndex) => (
                                <Th
                                    key={columnIndex}
                                    sort={column.sortable ? {
                                        sortBy,
                                        onSort: this.handleSort,
                                        columnIndex
                                    } : undefined}
                                    screenReaderText={column.screenReaderText}
                                >
                                    {column.title}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {rows.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                {row.map((cell, cellIndex) => (
                                    <Td key={cellIndex}>{cell}</Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
            </div>
        );
    }
}

class ExistingLagReportsTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            sortBy: {},
            expandedRows: new Set(),
            reports: this.props.reports || [],
            showLagReportModal: false,
            reportUrls: null,
            selectedReport: null
        };

        this.handleSetPage = (_evt, newPage) => {
            this.setState({ page: newPage });
        };

        this.handlePerPageSelect = (_evt, newPerPage) => {
            this.setState({ page: 1, perPage: newPerPage });
        };

        this.handleSort = this.handleSort.bind(this);
        this.handleViewReport = this.handleViewReport.bind(this);
        this.closeLagReportModal = this.closeLagReportModal.bind(this);
    }

    componentDidUpdate(prevProps) {
        if (prevProps.reports !== this.props.reports) {
            this.setState({ reports: this.props.reports || [] });
        }
    }

    handleSort(_event, index, direction) {
        this.setState({
            sortBy: {
                index,
                direction
            }
        });
    }

    handleViewReport(report) {
        // Construct the report URLs based on the report data
        const reportUrls = {
            base: report.path,
            json: report.hasJson ? `${report.path}/replication_analysis.json` : null,
            summary: report.hasJson ? `${report.path}/replication_analysis_summary.json` : null,
            html: report.hasHtml ? `${report.path}/replication_analysis.html` : null,
            csv: report.hasCsv ? `${report.path}/replication_analysis.csv` : null,
            png: report.hasPng ? `${report.path}/replication_analysis.png` : null
        };

        this.setState({
            showLagReportModal: true,
            reportUrls,
            selectedReport: report
        });

        // If there's an onSelectReport prop function, call it too
        if (this.props.onSelectReport) {
            this.props.onSelectReport(report);
        }
    }

    closeLagReportModal() {
        this.setState({
            showLagReportModal: false,
            reportUrls: null,
            selectedReport: null
        });
    }

    render() {
        const { page, perPage, sortBy, reports, showLagReportModal, reportUrls } = this.state;
        const { onSelectReport } = this.props;

        // Sort reports
        let sortedReports = [...reports];
        if (sortBy.index !== undefined) {
            sortedReports.sort((a, b) => {
                // Sort by report name
                if (sortBy.index === 0) {
                    return sortBy.direction === 'asc' ?
                        a.name.localeCompare(b.name) :
                        b.name.localeCompare(a.name);
                }

                // Sort by creation time
                if (sortBy.index === 1) {
                    return sortBy.direction === 'asc' ?
                        new Date(a.creationTime) - new Date(b.creationTime) :
                        new Date(b.creationTime) - new Date(a.creationTime);
                }

                // Sort by JSON
                if (sortBy.index === 2) {
                    return sortBy.direction === 'asc' ?
                        a.hasJson - b.hasJson :
                        b.hasJson - a.hasJson;
                }

                // Sort by HTML
                if (sortBy.index === 3) {
                    return sortBy.direction === 'asc' ?
                        a.hasHtml - b.hasHtml :
                        b.hasHtml - a.hasHtml;
                }

                // Sort by CSV
                if (sortBy.index === 4) {
                    return sortBy.direction === 'asc' ?
                        a.hasCsv - b.hasCsv :
                        b.hasCsv - a.hasCsv;
                }

                // Sort by PNG
                if (sortBy.index === 5) {
                    return sortBy.direction === 'asc' ?
                        a.hasPng - b.hasPng :
                        b.hasPng - a.hasPng;
                }
                return 0;
            });
        }

        const startIdx = (page - 1) * perPage;
        const paginatedReports = sortedReports.slice(startIdx, startIdx + perPage);
        const columns = [
            { title: _("Report Name"), transforms: [sortable] },
            { title: _("Creation Time"), transforms: [sortable] },
            { title: _("JSON"), transforms: [] },
            { title: _("HTML"), transforms: [] },
            { title: _("CSV"), transforms: [] },
            { title: _("PNG"), transforms: [] },
            { title: _("Actions"), transforms: [] }
        ];

        return (
            <>
                <div className="ds-margin-top-lg">
                    {reports.length > 0 ? (
                        <>
                            <Table
                                aria-label={_("Existing Reports")}
                                variant="compact"
                            >
                                <Thead>
                                    <Tr>
                                        {columns.map((column, columnIndex) => (
                                            <Th
                                                key={columnIndex}
                                                sort={{
                                                    columnIndex,
                                                    sortBy,
                                                    onSort: this.handleSort
                                                }}
                                            >
                                                {column.title}
                                            </Th>
                                        ))}
                                    </Tr>
                                </Thead>
                                <Tbody>
                                    {paginatedReports.map((report, rowIndex) => (
                                        <Tr key={rowIndex}>
                                            <Td>{report.name}</Td>
                                            <Td>{report.creationTime}</Td>
                                            <Td>{report.hasJson ? <CheckIcon /> : <MinusIcon />}</Td>
                                            <Td>{report.hasHtml ? <CheckIcon /> : <MinusIcon />}</Td>
                                            <Td>{report.hasCsv ? <CheckIcon /> : <MinusIcon />}</Td>
                                            <Td>{report.hasPng ? <CheckIcon /> : <MinusIcon />}</Td>
                                            <Td>
                                                <Button
                                                    variant="primary"
                                                    onClick={() => this.handleViewReport(report)}
                                                >
                                                    {_("View Report")}
                                                </Button>
                                            </Td>
                                        </Tr>
                                    ))}
                                </Tbody>
                            </Table>
                            <Pagination
                                itemCount={reports.length}
                                widgetId="pagination-options-menu-bottom"
                                perPage={perPage}
                                page={page}
                                variant="bottom"
                                onSetPage={this.handleSetPage}
                                onPerPageSelect={this.handlePerPageSelect}
                            />
                        </>
                    ) : (
                        <EmptyState>
                            <EmptyStateIcon icon={SearchIcon} />
                            <Title headingLevel="h4" size="lg">
                                {_("No reports found")}
                            </Title>
                            <EmptyStateBody>
                                {_("No replication log analysis reports were found in the selected directory.")}
                            </EmptyStateBody>
                        </EmptyState>
                    )}
                </div>

                {showLagReportModal && (
                    <LagReportModal
                        showModal={showLagReportModal}
                        closeHandler={this.closeLagReportModal}
                        reportUrls={reportUrls}
                    />
                )}
            </>
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
    handlePokeAgmt: PropTypes.func,
};

AgmtTable.defaultProps = {
    agmts: [],
};

WinsyncAgmtTable.propTypes = {
    agmts: PropTypes.array,
    handlePokeAgmt: PropTypes.func,
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

ExistingLagReportsTable.propTypes = {
    reports: PropTypes.array,
    onSelectReport: PropTypes.func
};

ExistingLagReportsTable.defaultProps = {
    reports: [],
    onSelectReport: () => {}
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
    ExistingLagReportsTable
};
