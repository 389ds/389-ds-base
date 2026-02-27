import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Pagination,
    SearchInput,
    Switch,
    Text,
    TextContent,
    TextInput,
    Tooltip,
} from '@patternfly/react-core';
import {
    Table,
	SortByDirection,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
    ActionsColumn,
    ExpandableRowContent
} from '@patternfly/react-table';
import PropTypes from "prop-types";
import TypeaheadSelect from "../../dsBasicComponents.jsx";

const _ = cockpit.gettext;

class KeyTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            hasRows: false,
            columns: [
                { title: _("Cipher"), sortable: true },
                { title: _("Key Identifier"), sortable: true },
                { title: _("State"), sortable: true },
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
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let hasRows = true;

        for (const ServerKey of this.props.ServerKeys) {
            rows.push([
                ServerKey.attrs.cipher,
                ServerKey.attrs.key_id,
                ServerKey.attrs.state
            ]);
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Orphan keys")] }];
            columns = [{ title: _("Orphan keys") }];
            hasRows = false;
        }

        this.setState({
            rows,
            columns,
            hasRows
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Delete Key"),
            onClick: () => {
                if (rowData[1]) {
                    this.props.delKey(rowData[1]);
                }
            }
        }
    ];

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) =>
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );

        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    render() {
        const { perPage, page, sortBy, rows, columns, hasRows } = this.state;
        const startIdx = (perPage * page) - perPage;
        const tableRows = rows.slice(startIdx, startIdx + perPage);

        return (
            <div className="ds-margin-top-lg">
                <Tooltip
                    content={
                        <div>
                            <p>
                                {_("An orphan key is a private key in the NSS DB for which there is NO cert with the corresponding public key. An orphan key is created during CSR creation, when the certificate associated with a CSR has been imported into the NSS DB its orphan state will be removed.")}
                                <br /><br />
                                {_("Make sure an orphan key is not associated with a submitted CSR before you delete it.")}
                            </p>
                        </div>
                    }
                >
                    <a className="ds-font-size-sm">{_("What is an orphan key?")}</a>
                </Tooltip>
                <Table
                    className="ds-margin-top"
                    aria-label="orph key table"
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
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                {hasRows &&
                    <Pagination
                        itemCount={rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={perPage}
                        page={page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />
                }
            </div>
        );
    }
}

class CSRTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("Name"), sortable: true },
                { title: _("Subject DN"), sortable: true },
                { title: _("Subject Alternative Names"), sortable: true },
                { title: _("Modification Date"), sortable: true },
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
        this.handleSearchChange = this.handleSearchChange.bind(this);
    }

    handleSort(_event, index, direction) {
        const sortedRows = [...this.state.rows].sort((a, b) =>
            (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0)
        );
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Delete CSR"),
            onClick: () => this.props.delCSR(rowData[0])
        },
        {
            title: _("View CSR"),
            onClick: () => this.props.viewCSR(rowData[0])
        }
    ];

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let hasRows = true;

        for (const ServerCSR of this.props.ServerCSRs) {
            rows.push([
                ServerCSR.attrs.name,
                ServerCSR.attrs.subject,
                ServerCSR.attrs.subject_alt_names.join(", "),
                ServerCSR.attrs.modified
            ]);
        }

        if (rows.length === 0) {
            rows = [{ cells: [_("No Certificate Signing Requests")] }];
            columns = [{ title: _("Certificate Signing Requests") }];
            hasRows = false;
        }

        this.setState({
            rows,
            columns,
            hasRows,
        });
    }

    handleSearchChange(event, value) {
        const rows = [];

        for (const cert of this.props.ServerCSRs) {
            const val = value.toLowerCase();

            if (val !== "" && cert.attrs.name.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject_alt_names.join().toLowerCase().indexOf(val) === -1 &&
                cert.attrs.modified.toLowerCase().indexOf(val) === -1) {
                continue;
            }

            rows.push([
                cert.attrs.name,
                cert.attrs.subject,
                cert.attrs.subject_alt_names.join(", "),
                cert.attrs.modified
            ]);
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    render() {
        const { perPage, page, sortBy, rows, columns, hasRows } = this.state;
        const tableRows = rows.slice((page - 1) * perPage, page * perPage);

        return (
            <div className="ds-margin-top-lg">
                {hasRows &&
                    <SearchInput
                        placeholder={_("Search CSRs")}
                        value={this.state.value}
                        onChange={this.handleSearchChange}
                        onClear={(evt) => this.handleSearchChange(evt, '')}
                    />
                }
                <Table
                    className="ds-margin-top"
                    aria-label="csr table"
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
                                {Array.isArray(row) ? (
                                    row.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                ) : (
                                    row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))
                                )}
                                {hasRows && (
                                    <Td isActionCell>
                                        <ActionsColumn
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                )}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
                {hasRows &&
                    <Pagination
                        itemCount={rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={perPage}
                        page={page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />
                }
            </div>
        );
    }
}

class CertTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            dropdownIsOpen: false,
            hasRows: false,
            columns: [
                {
                    title: _("Nickname"),
                    sortable: true
                },
                { title: _("Subject DN"), sortable: true },
                { title: _("Expiration Date"), sortable: true },
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
    }

    handleSort(_event, columnIndex, direction) {
        const rows = [...this.state.rows];

        rows.sort((a, b) => (a.cells[columnIndex].content > b.cells[columnIndex].content) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            rows.reverse();
        }

        this.setState({
            sortBy: {
                index: columnIndex,
                direction
            },
            rows,
            page: 1,
        });
    }

    getExpandedRow(issuer, flags) {
        return (
            <Grid className="ds-left-indent-lg">
                <GridItem span={3}>{_("Issuer DN:")}</GridItem>
                <GridItem span={9}><b>{issuer}</b></GridItem>
                <GridItem span={3}>{_("Trust Flags:")}</GridItem>
                <GridItem span={9}><b>{flags}</b></GridItem>
            </Grid>
        );
    }

    componentDidMount() {
        let rows = [];
        let columns = this.state.columns;
        let count = 0;
        let hasRows = true;

        for (const cert of this.props.certs) {
            rows.push({
                isOpen: false,
                cells: [
                    { content: cert.attrs.nickname },
                    { content: cert.attrs.subject },
                    { content: cert.attrs.expires }
                ],
                issuer: cert.attrs.issuer,
                flags: cert.attrs.flags,
                originalData: cert.attrs  // Store the original data for expansion
            });
            count += 1;
        }
        if (rows.length === 0) {
            rows = [{ cells: [{ content: _("No Certificates") }] }];
            columns = [{ title: _("Certificates") }];
            hasRows = false;
        }
        this.setState({
            rows,
            columns,
            hasRows,
        });
    }

    handleCollapse(_event, rowIndex, isExpanding) {
        const rows = [...this.state.rows];
        const index = (this.state.perPage * (this.state.page - 1) * 2) + rowIndex;
        rows[index].isOpen = isExpanding;
        this.setState({ rows });
    }

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const cert of this.props.certs) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val !== "" && cert.attrs.nickname.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.issuer.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.expires.toLowerCase().indexOf(val) === -1) {
                // Not a match
                continue;
            }

            rows.push(
                {
                    isOpen: false,
                    cells: [
                        { content: cert.attrs.nickname },
                        { content: cert.attrs.subject },
                        { content: cert.attrs.expires }
                    ],
                    issuer: cert.attrs.issuer,
                    flags: cert.attrs.flags,
                }
            );
            count += 1;
        }

        this.setState({
            rows,
            value,
            page: 1,
        });
    }

    getActionsForRow = (rowData) => [
        {
            title: _("Edit Trust Flags"),
            onClick: () => this.props.editCert(rowData.cells[0].content, rowData.flags)
        },
        {
            title: _("Export Certificate"),
            onClick: () => this.props.exportCert(rowData.cells[0].content)
        },
        {
            isSeparator: true
        },
        {
            title: _("Delete Certificate"),
            onClick: () => this.props.delCert(rowData.cells[0].content)
        }
    ];

    render() {
        const { perPage, page, sortBy, rows, columns, hasRows } = this.state;
        const startIdx = ((perPage * page) - perPage) * 2;
        const tableRows = rows.slice(startIdx, startIdx + (perPage * 2));

        return (
            <div className="ds-margin-top-lg">
                {hasRows &&
                    <SearchInput
                        placeholder={_("Search Certificates")}
                        value={this.state.value}
                        onChange={this.handleSearchChange}
                        onClear={(evt) => this.handleSearchChange(evt, '')}
                    />}
                <Table
                    aria-label="cert table"
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
                                        columnIndex
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            <Th screenReaderText="Actions" />
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => this.handleCollapse(null, rowIndex, !row.isOpen)
                                        }}
                                    />
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>
                                            {cell.content}
                                        </Td>
                                    ))}
                                    <Td isActionCell>
                                        <ActionsColumn
                                            items={this.getActionsForRow(row)}
                                        />
                                    </Td>
                                </Tr>
                                {row.isOpen && (
                                    <Tr isExpanded={true}>
                                        <Td colSpan={columns.length + 2}>
                                            <ExpandableRowContent>
                                                {this.getExpandedRow(row.issuer, row.flags)}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                {hasRows &&
                    <Pagination
                        itemCount={this.state.rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={perPage}
                        page={page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />}
            </div>
        );
    }
}

// Future - https://github.com/389ds/389-ds-base/issues/3548
class CRLTable extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            hasRows: false,
            columns: [
                { title: _("Issued By"), sortable: true },
                { title: _("Effective Date"), sortable: true },
                { title: _("Next Update"), sortable: true },
                { title: _("Type"), sortable: true },
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
        this.handleSearchChange = this.handleSearchChange.bind(this);
    }

    getActionsForRow = (rowData) => [
        {
            title: _("View CRL"),
            onClick: () => this.props.editConfig(rowData.cells[0], rowData.cells[1], rowData.credsBindpw, rowData.pwInteractive)
        },
        {
            title: _("Delete CRL"),
            onClick: () => this.props.deleteConfig(rowData.cells[0])
        }
    ];

    handleSort(_event, index, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows pairings into a sortable array
        for (let idx = 0; idx < this.state.rows.length; idx += 2) {
            sorted_rows.push({
                1: this.state.rows[idx].cells[0],
                2: this.state.rows[idx].cells[1],
                3: this.state.rows[idx].cells[2],
                issuer: this.state.rows[idx].issuer,
                flags: this.state.rows[idx].flags,
                expandedRow: this.state.rows[idx + 1]
            });
        }

        // Sort and rebuild rows
        sorted_rows.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_rows.reverse();
        }

        for (const srow of sorted_rows) {
            rows.push({
                isOpen: false,
                cells: [srow[1], srow[2], srow[3]],
                issuer: srow.issuer,
                flags: srow.flags,
            });
            srow.expandedRow.parent = count;
            rows.push(srow.expandedRow);
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

    handleSearchChange(event, value) {
        const rows = [];
        let count = 0;

        for (const cert of this.props.certs) {
            const val = value.toLowerCase();

            // Check for matches of all the parts
            if (val !== "" && cert.attrs.nickname.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.subject.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.issuer.toLowerCase().indexOf(val) === -1 &&
                cert.attrs.expires.toLowerCase().indexOf(val) === -1) {
                // Not a match
                continue;
            }

            rows.push(
                {
                    isOpen: false,
                    cells: [cert.attrs.nickname, cert.attrs.subject, cert.attrs.expires],
                    issuer: cert.attrs.issuer,
                    flags: cert.attrs.flags,

                },
                {
                    parent: count,
                    fullWidth: true,
                    cells: [{ title: this.getExpandedRow(cert.attrs.issuer, cert.attrs.flags) }]
                },
            );
            count += 2;
        }

        this.setState({
            rows,
            value,
            page: 1,
            hasRows: rows.length !== 0,
        });
    }

    render() {
        const tableRows = this.state.rows.slice(
            (this.state.page - 1) * this.state.perPage,
            this.state.page * this.state.perPage
        );

        return (
            <div className="ds-margin-top">
                <SearchInput
                    placeholder={_("Search CRL's")}
                    value={this.state.value}
                    onChange={this.handleSearchChange}
                    onClear={(evt) => this.handleSearchChange(evt, '')}
                />
                <Table
                    aria-label="CRL Table"
                    variant="compact"
                >
                    <Thead>
                        <Tr>
                            {this.state.columns.map((column, idx) => (
                                <Th
                                    key={idx}
                                    sort={column.sortable ? {
                                        sortBy: this.state.sortBy,
                                        onSort: this.handleSort,
                                        columnIndex: idx
                                    } : undefined}
                                >
                                    {column.title}
                                </Th>
                            ))}
                            {this.state.hasRows && <Th screenReaderText="Actions" />}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {tableRows.map((row, rowIndex) => (
                            <React.Fragment key={rowIndex}>
                                <Tr>
                                    {row.cells.map((cell, cellIndex) => (
                                        <Td key={cellIndex}>{cell}</Td>
                                    ))}
                                    {this.state.hasRows && (
                                        <Td isActionCell>
                                            <ActionsColumn
                                                items={this.getActionsForRow(row)}
                                            />
                                        </Td>
                                    )}
                                </Tr>
                                {row.isOpen && (
                                    <Tr isExpanded>
                                        <Td colSpan={this.state.columns.length + 1}>
                                            <ExpandableRowContent>
                                                {this.getExpandedRow(row.issuer, row.flags)}
                                            </ExpandableRowContent>
                                        </Td>
                                    </Tr>
                                )}
                            </React.Fragment>
                        ))}
                    </Tbody>
                </Table>
                {this.state.hasRows && (
                    <Pagination
                        itemCount={this.state.rows.length}
                        widgetId="pagination-options-menu-bottom"
                        perPage={this.state.perPage}
                        page={this.state.page}
                        variant="bottom"
                        onSetPage={this.handleSetPage}
                        onPerPageSelect={this.handlePerPageSelect}
                    />
                )}
            </div>
        );
    }
}

// Finds whether a certificate nickname is already assigned to another module.
function getAssignedModuleByCert(modules, certNickname, currentModuleName = "") {
    if (!certNickname) {
        return null;
    }

    return modules.find(module =>
        module.certNickname === certNickname && module.name !== currentModuleName
    ) || null;
}

// Validates required encryption-module form fields and uniqueness constraints.
function validateEncryptionModuleForm({
    mode = "create",
    name = "",
    certNickname = "",
    token = "",
    modules = [],
    currentModuleName = "",
}) {
    const errors = {};

    if (mode === "create" && name.trim() === "") {
        errors.name = _("Module name is required.");
    }
    if (certNickname.trim() === "") {
        errors.certNickname = _("Server certificate nickname is required.");
    }
    if (token.trim() === "") {
        errors.token = _("Token is required.");
    }

    if (!errors.certNickname) {
        const assignedModule = getAssignedModuleByCert(modules, certNickname.trim(), currentModuleName);
        if (assignedModule) {
            errors.certNickname = cockpit.format(
                _("Certificate nickname is already assigned to module '$0'."),
                assignedModule.name
            );
        }
    }

    return {
        valid: Object.keys(errors).length === 0,
        errors,
    };
}

// Modal used for creating or editing a single encryption module.
function EncryptionModuleModal({
    isOpen,
    mode,
    formState,
    formErrors,
    certOptions,
    onFormChange,
    onCertSelect,
    onClose,
    onSave,
    saving,
    saveDisabled,
}) {
    const isEdit = mode === "edit";
    const title = isEdit ? _("Edit Encryption Module") : _("Add Encryption Module");
    const btnLabel = isEdit ? _("Save") : _("Create");

    const selectedCert = formState.certNickname ? [formState.certNickname] : [];
    const validatedName = formErrors.name ? "error" : "default";
    const validatedToken = formErrors.token ? "error" : "default";
    const validatedCert = formErrors.certNickname ? "error" : "default";

    const formattedCertOptions = certOptions.map(option => {
        if (typeof option === "string") {
            return option;
        }
        const inUseSuffix = option.inUseByModule
            ? cockpit.format(_(" (in use by '$0')"), option.inUseByModule)
            : "";
        return {
            value: option.nickname,
            label: `${option.nickname}${inUseSuffix}`,
            isDisabled: option.selectable === false,
        };
    });

    return (
        <Modal
            variant={ModalVariant.medium}
            title={title}
            isOpen={isOpen}
            onClose={onClose}
            aria-labelledby="encryption-module-modal"
            actions={[
                <Button
                    key="save"
                    variant="primary"
                    onClick={onSave}
                    isDisabled={saving || saveDisabled}
                    isLoading={saving}
                    spinnerAriaValueText={saving ? _("Saving") : undefined}
                >
                    {btnLabel}
                </Button>,
                <Button key="cancel" variant="link" onClick={onClose} isDisabled={saving}>
                    {_("Cancel")}
                </Button>,
            ]}
        >
            <Form isHorizontal autoComplete="off">
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={4}>{_("Module Name")}</GridItem>
                    <GridItem span={8}>
                        <TextInput
                            id="name"
                            value={formState.name}
                            onChange={(_event, value) => onFormChange("name", value)}
                            isDisabled={isEdit || saving}
                            validated={validatedName}
                        />
                        {formErrors.name && (
                            <Text component="small" className="ds-margin-top-sm">{formErrors.name}</Text>
                        )}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={4}>{_("Server Certificate Name")}</GridItem>
                    <GridItem span={8}>
                        <TypeaheadSelect
                            selected={selectedCert}
                            onSelect={(_event, value) => onCertSelect(value)}
                            onClear={() => onCertSelect("")}
                            options={formattedCertOptions}
                            isCreatable
                            onCreateOption={(value) => onCertSelect(value)}
                            allowCustomValues
                            placeholder={_("Type a server certificate nickname...")}
                            validationMessage={formErrors.certNickname}
                            validated={validatedCert}
                            ariaLabel={_("Encryption module certificate selector")}
                        />
                        {formErrors.certNickname && (
                            <Text component="small" className="ds-margin-top-sm">{formErrors.certNickname}</Text>
                        )}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={4}>{_("Token")}</GridItem>
                    <GridItem span={8}>
                        <TextInput
                            id="token"
                            value={formState.token}
                            onChange={(_event, value) => onFormChange("token", value)}
                            isDisabled={saving}
                            validated={validatedToken}
                        />
                        {formErrors.token && (
                            <Text component="small" className="ds-margin-top-sm">{formErrors.token}</Text>
                        )}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={4}>{_("Server Certificate Extract File")}</GridItem>
                    <GridItem span={8}>
                        <TextInput
                            id="serverCertExtractFile"
                            value={formState.serverCertExtractFile}
                            onChange={(_event, value) => onFormChange("serverCertExtractFile", value)}
                            isDisabled={saving}
                        />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={4}>{_("Server Key Extract File")}</GridItem>
                    <GridItem span={8}>
                        <TextInput
                            id="serverKeyExtractFile"
                            value={formState.serverKeyExtractFile}
                            onChange={(_event, value) => onFormChange("serverKeyExtractFile", value)}
                            isDisabled={saving}
                        />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={4}>{_("Activated")}</GridItem>
                    <GridItem span={8}>
                        <Switch
                            id="module-activated"
                            isChecked={formState.activated}
                            onChange={(_event, checked) => onFormChange("activated", checked)}
                            isDisabled={saving}
                            label={_("Enabled")}
                            labelOff={_("Disabled")}
                        />
                    </GridItem>
                </Grid>
                {formErrors._form && (
                    <Grid className="ds-margin-top">
                        <GridItem span={12}>
                            <Text component="small">{formErrors._form}</Text>
                        </GridItem>
                    </Grid>
                )}
            </Form>
        </Modal>
    );
}

// Main encryption-module table view with inline add/edit modal workflow.
function EncryptionModuleTable(props) {
    const {
        modules,
        rows,
        certOptions,
        loading,
        loadError,
        partialDataCount,
        actionInProgress,
        onCreateModule,
        onEditModule,
        onToggleModule,
        onDeleteModule,
    } = props;

    const [isModalOpen, setIsModalOpen] = React.useState(false);
    const [modalMode, setModalMode] = React.useState("create");
    const [editingModuleName, setEditingModuleName] = React.useState("");
    const [expandedRows, setExpandedRows] = React.useState({});
    const [formErrors, setFormErrors] = React.useState({});
    const [formState, setFormState] = React.useState({
        name: "",
        certNickname: "",
        token: "internal (software)",
        serverCertExtractFile: "",
        serverKeyExtractFile: "",
        activated: false,
    });
    const [editInitialState, setEditInitialState] = React.useState(null);

    const modalCertOptions = React.useMemo(() => {
        const options = Array.isArray(certOptions) ? [...certOptions] : [];
        const current = formState.certNickname?.trim();
        if (current && !options.some(option => option.nickname === current)) {
            options.push({
                nickname: current,
                inUseByModule: null,
                selectable: true,
                isAdHoc: true,
            });
        }
        return options;
    }, [certOptions, formState.certNickname]);

    const openCreateModal = () => {
        setModalMode("create");
        setEditingModuleName("");
        setEditInitialState(null);
        setFormErrors({});
        setFormState({
            name: "",
            certNickname: "",
            token: "internal (software)",
            serverCertExtractFile: "",
            serverKeyExtractFile: "",
            activated: false,
        });
        setIsModalOpen(true);
    };

    const openEditModal = module => {
        const initialState = {
            name: module.name || "",
            certNickname: module.certNickname || "",
            token: module.token || "internal (software)",
            serverCertExtractFile: module.serverCertExtractFile || "",
            serverKeyExtractFile: module.serverKeyExtractFile || "",
            activated: module.activated === "on",
        };
        setModalMode("edit");
        setEditingModuleName(module.name);
        setEditInitialState(initialState);
        setFormErrors({});
        setFormState(initialState);
        setIsModalOpen(true);
    };

    const closeModal = () => {
        if (actionInProgress) {
            return;
        }
        setIsModalOpen(false);
    };

    const handleFormChange = (field, value) => {
        setFormState(prev => ({ ...prev, [field]: value }));
        setFormErrors(prev => {
            if (!prev[field] && !prev._form) {
                return prev;
            }
            const next = { ...prev };
            delete next[field];
            delete next._form;
            return next;
        });
    };

    const normalizedFormState = React.useMemo(() => ({
        name: formState.name.trim(),
        certNickname: formState.certNickname.trim(),
        token: formState.token.trim(),
        serverCertExtractFile: formState.serverCertExtractFile.trim(),
        serverKeyExtractFile: formState.serverKeyExtractFile.trim(),
        activated: !!formState.activated,
    }), [formState]);

    const isUnchangedEdit = React.useMemo(() => {
        if (modalMode !== "edit" || !editInitialState) {
            return false;
        }
        return normalizedFormState.name === editInitialState.name.trim() &&
            normalizedFormState.certNickname === editInitialState.certNickname.trim() &&
            normalizedFormState.token === editInitialState.token.trim() &&
            normalizedFormState.serverCertExtractFile === editInitialState.serverCertExtractFile.trim() &&
            normalizedFormState.serverKeyExtractFile === editInitialState.serverKeyExtractFile.trim() &&
            normalizedFormState.activated === !!editInitialState.activated;
    }, [modalMode, editInitialState, normalizedFormState]);

    const liveValidation = React.useMemo(() => validateEncryptionModuleForm({
        mode: modalMode,
        name: formState.name,
        certNickname: formState.certNickname,
        token: formState.token,
        modules,
        currentModuleName: editingModuleName,
    }), [modalMode, formState, modules, editingModuleName]);

    const isSaveDisabled = actionInProgress || !liveValidation.valid || isUnchangedEdit;

    const handleSave = () => {
        if (!liveValidation.valid) {
            setFormErrors(liveValidation.errors);
            return;
        }
        if (isUnchangedEdit) {
            setFormErrors({
                _form: _("No changes detected."),
            });
            return;
        }

        setFormErrors({});
        const payload = normalizedFormState;
        if (modalMode === "edit") {
            onEditModule(editingModuleName, payload, () => setIsModalOpen(false));
        } else {
            onCreateModule(payload, () => setIsModalOpen(false));
        }
    };

    const toggleExpand = moduleName => {
        setExpandedRows(prev => ({
            ...prev,
            [moduleName]: !prev[moduleName],
        }));
    };

    return (
        <div className="ds-margin-top-lg">
            <Grid>
                <GridItem span={12}>
                    <Button variant="primary" onClick={openCreateModal} isDisabled={loading || actionInProgress}>
                        {_("Add Encryption Module")}
                    </Button>
                </GridItem>
            </Grid>
            {loadError && (
                <TextContent className="ds-margin-top">
                    <Text component="small">{cockpit.format(_("Unable to load encryption modules: $0"), loadError)}</Text>
                </TextContent>
            )}
            {!loadError && partialDataCount > 0 && (
                <TextContent className="ds-margin-top">
                    <Text component="small">
                        {cockpit.format(_("Some encryption module entries were skipped because required data was incomplete: $0"), partialDataCount)}
                    </Text>
                </TextContent>
            )}
            <Table className="ds-margin-top" aria-label={_("Encryption module table")} variant="compact">
                <Thead>
                    <Tr>
                        <Th screenReaderText={_("Row expansion")} />
                        <Th>{_("Module Name")}</Th>
                        <Th>{_("Server Certificate Name")}</Th>
                        <Th>{_("Activated")}</Th>
                        <Th screenReaderText={_("Actions")} />
                    </Tr>
                </Thead>
                <Tbody>
                    {loading ? (
                        <Tr>
                            <Td />
                            <Td colSpan={4}>{_("Loading encryption modules...")}</Td>
                        </Tr>
                    ) : rows.length === 0 ? (
                        <Tr>
                            <Td />
                            <Td colSpan={4}>
                                {loadError
                                    ? _("Unable to display encryption modules due to load errors.")
                                    : _("No Encryption Modules")}
                            </Td>
                        </Tr>
                    ) : (
                        rows.map((row, rowIndex) => {
                            const module = modules.find(item => item.name === row.name) || row;
                            const isExpanded = !!expandedRows[row.name];
                            const rowActions = [
                                {
                                    title: _("Edit"),
                                    onClick: () => openEditModal(module),
                                    isDisabled: actionInProgress,
                                },
                                {
                                    title: row.activated === "on" ? _("Disable") : _("Enable"),
                                    onClick: () => onToggleModule(module, row.activated !== "on"),
                                    isDisabled: actionInProgress,
                                },
                                { isSeparator: true },
                                {
                                    title: _("Delete"),
                                    onClick: () => onDeleteModule(module),
                                    isDisabled: actionInProgress,
                                },
                            ];

                            return (
                                <React.Fragment key={row.name}>
                                    <Tr>
                                        <Td
                                            expand={{
                                                rowIndex,
                                                isExpanded,
                                                onToggle: () => toggleExpand(row.name),
                                            }}
                                        />
                                        <Td>{row.name}</Td>
                                        <Td>{row.certNickname}</Td>
                                        <Td>{row.activated === "on" ? _("On") : _("Off")}</Td>
                                        <Td isActionCell>
                                            <ActionsColumn items={rowActions} />
                                        </Td>
                                    </Tr>
                                    {isExpanded && (
                                        <Tr isExpanded>
                                            <Td colSpan={5}>
                                                <ExpandableRowContent>
                                                    <Grid className="ds-left-indent-md">
                                                        <GridItem span={3}>{_("Token:")}</GridItem>
                                                        <GridItem span={9}><b>{module.token || "-"}</b></GridItem>
                                                        <GridItem span={3}>{_("Server Cert Extract File:")}</GridItem>
                                                        <GridItem span={9}><b>{module.serverCertExtractFile || "-"}</b></GridItem>
                                                        <GridItem span={3}>{_("Server Key Extract File:")}</GridItem>
                                                        <GridItem span={9}><b>{module.serverKeyExtractFile || "-"}</b></GridItem>
                                                    </Grid>
                                                </ExpandableRowContent>
                                            </Td>
                                        </Tr>
                                    )}
                                </React.Fragment>
                            );
                        })
                    )}
                </Tbody>
            </Table>

            <EncryptionModuleModal
                isOpen={isModalOpen}
                mode={modalMode}
                formState={formState}
                formErrors={formErrors}
                certOptions={modalCertOptions}
                onFormChange={handleFormChange}
                onCertSelect={value => handleFormChange("certNickname", value)}
                onClose={closeModal}
                onSave={handleSave}
                saving={actionInProgress}
                saveDisabled={isSaveDisabled}
            />
        </div>
    );
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
};

CSRTable.propTypes = {
    ServerCSRs: PropTypes.array,
    delCSR: PropTypes.func,
    viewCSR: PropTypes.func,
};

CSRTable.defaultProps = {
    ServerCSRs: [],
};

KeyTable.propTypes = {
    ServerKeys: PropTypes.array,
    delKey: PropTypes.func,
};

KeyTable.defaultProps = {
    ServerKeys: [],
};

EncryptionModuleTable.propTypes = {
    modules: PropTypes.array,
    rows: PropTypes.array,
    certOptions: PropTypes.array,
    loading: PropTypes.bool,
    loadError: PropTypes.string,
    partialDataCount: PropTypes.number,
    actionInProgress: PropTypes.bool,
    onCreateModule: PropTypes.func,
    onEditModule: PropTypes.func,
    onToggleModule: PropTypes.func,
    onDeleteModule: PropTypes.func,
};

export {
    CertTable,
    CRLTable,
    CSRTable,
    EncryptionModuleTable,
    KeyTable,
};
