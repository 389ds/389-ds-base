import cockpit from "cockpit";
import React from 'react';
import {
    Button,
    Modal, ModalVariant,
    TextArea,
    Pagination, PaginationVariant,
} from '@patternfly/react-core';
import {
	expandable,
	sortable,
	SortByDirection,
	ExpandableRowContent,
    ActionsColumn,
    Table,
    Thead,
    Tbody,
    Tr,
    Th,
    Td
} from '@patternfly/react-table';
import {
    retrieveAllAcis, modifyLdapEntry
} from '../lib/utils.jsx';
import {
    getAciActualName, isAciPermissionAllow
} from '../lib/aciParser.jsx';
import AddNewAci from './operations/aciNew.jsx';
import { DoubleConfirmModal } from "../../notifications.jsx";

const _ = cockpit.gettext;

class AciWizard extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            firstLoad: true,
            page: 1,
            perPage: 10,
            value: '',
            sortBy: {},
            rows: [],
            columns: [
                { title: _("ACI Name"), transforms: [sortable], cellFormatters: [expandable] },
                { title: _("ACI Rule Type"), transforms: [sortable] },
            ],
            actions: [
                {
                    title: _("Edit ACI"),
                    onClick: (event, rowId, rowData, extra) => this.showEditAci(rowData)
                },
                {
                    isSeparator: true
                },
                {
                    title: _("Remove ACI"),
                    onClick: (event, rowId, rowData, extra) => this.showDeleteConfirm(rowData)
                },
            ],
            isWizardOpen: false,
            isManualOpen: false,
            showModal: true,
            showConfirmDelete: false,
            modalSpinning: false,
            modalChecked: false,
            showEditAci: false,
            attrName: "",
            aciText: "",
            aciTextNew: "",
        };

        this.onToggle = isOpen => {
            this.setState({
                isOpen
            });
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

        this.handleCloseModal = () => {
            this.setState({
                showModal: false,
            });
        };

        this.onChange = (e) => {
            // Handle the modal changes
            const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
            this.setState({
                [e.target.id]: value,
            });
        };

        this.showDeleteConfirm = (rowData) => {
            this.setState({
                aciText: rowData.fullAci,
                aciName: rowData.cells[0],
                showDeleteConfirm: true,
                modalChecked: false,
                modalSpinning: false,
            });
        };

        this.closeDeleteConfirm = (rowData) => {
            this.setState({
                aciText: "",
                showDeleteConfirm: false,
                modalChecked: false,
                modalSpinning: false,
            });
        };

        this.showEditAci = (rowData) => {
            this.setState({
                aciText: rowData.fullAci,
                aciTextNew: rowData.fullAci,
                showEditAci: true,
                modalChecked: false,
                modalSpinning: false,
            });
        };

        this.handleCloseEditAci = () => {
            this.setState({
                showEditAci: false,
            });
        };

        this.handleResetACIText = () => {
            const orig = this.state.aciText;
            this.setState({
                aciTextNew: orig
            });
        };

        this.handleSaveACI = () => {
            const params = { serverId: this.props.editorLdapServer };
            const ldifArray = [];
            ldifArray.push(`dn: ${this.props.wizardEntryDn}`);
            ldifArray.push('changetype: modify');
            ldifArray.push('delete: aci');
            ldifArray.push(`aci: ${this.state.aciText}`);
            ldifArray.push('-');
            ldifArray.push(`add: aci`);
            ldifArray.push(`aci: ${this.state.aciTextNew}`);

            this.setState({
                modalSpinning: true
            });

            modifyLdapEntry(params, ldifArray, (result) => {
                if (result.errorCode === 0) {
                    this.props.addNotification(
                        "success",
                        _("Successfully replace ACI")
                    );
                    this.buildAciTable();
                } else {
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to update ACI: error $0 - $1"), result.errorCode, result.output)
                    );
                }
                this.handleCloseEditAci();
                const opInfo = { // This is what refreshes treeView
                    operationType: 'MODIFY',
                    resultCode: result.errorCode,
                    time: Date.now()
                };
                this.props.setWizardOperationInfo(opInfo);
            });
        };

        this.deleteACI = () => {
            const params = { serverId: this.props.editorLdapServer };
            const ldifArray = [];
            ldifArray.push(`dn: ${this.props.wizardEntryDn}`);
            ldifArray.push('changetype: modify');
            ldifArray.push('delete: aci');
            ldifArray.push(`aci: ${this.state.aciText}`);

            this.setState({
                modalSpinning: true
            });

            modifyLdapEntry(params, ldifArray, (result) => {
                if (result.errorCode === 0) {
                    this.props.addNotification(
                        "success",
                        _("Successfully removed ACI")
                    );
                    this.buildAciTable();
                } else {
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to remove ACI: error $0 - $1"), result.errorCode, result.output)
                    );
                }
                this.closeDeleteConfirm();
                const opInfo = { // This is what refreshes treeView
                    operationType: 'MODIFY',
                    resultCode: result.errorCode,
                    time: Date.now()
                };
                this.props.setWizardOperationInfo(opInfo);
            });
        };

        this.handleAddAciManual = () => {
            const params = { serverId: this.props.editorLdapServer };
            const ldifArray = [];
            ldifArray.push(`dn: ${this.props.wizardEntryDn}`);
            ldifArray.push('changetype: modify');
            ldifArray.push(`add: aci`);
            ldifArray.push(`aci: ${this.state.aciTextNew}`);

            this.setState({
                modalSpinning: true
            });

            modifyLdapEntry(params, ldifArray, (result) => {
                if (result.errorCode === 0) {
                    this.props.addNotification(
                        "success",
                        _("Successfully added ACI")
                    );
                    this.buildAciTable();
                } else {
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to add ACI: error $0 - $1"), result.errorCode, result.output)
                    );
                }
                this.handleToggleManual();
                const opInfo = { // This is what refreshes the treeView
                    operationType: 'MODIFY',
                    resultCode: result.errorCode,
                    time: Date.now()
                };
                this.props.setWizardOperationInfo(opInfo);
            });
        };

        // this.buildAciTable = this.buildAciTable.bind(this);
        this.handleSort = this.handleSort.bind(this);
        this.handleCollpase = this.handleCollpase.bind(this);
    } // End constructor

    componentDidMount () {
        this.buildAciTable(this.state.firstLoad);
        this.setState({
            firstLoad: false
        });
    }

    buildAciTable = (firstLoad) => {
        const params = {
            serverId: this.props.editorLdapServer,
            baseDn: this.props.wizardEntryDn
        };
        retrieveAllAcis(params, (resultArray) => {
            const rows = [];
            const columns = [...this.state.columns];
            const actions = this.state.actions;
            let count = 0;

            if (resultArray.length !== 0) {
                const myAciArray = [...resultArray[0].aciArray];
                for (const anAci of myAciArray) {
                    const aciName = getAciActualName(anAci);
                    const aciAllow = isAciPermissionAllow(anAci);

                    rows.push({
                        isOpen: false,
                        cells: [aciName, aciAllow ? "allow" : "deny"],
                        fullAci: anAci
                    });
                    rows.push({
                        parent: count,
                        fullWidth: true,
                        cells: [anAci]
                    });
                    count += 2;
                }
            }

            this.setState({
                rows,
                columns,
                actions,
            }, () => { if (!firstLoad) { this.props.onReload() } }); // refreshes table view
        });
    };

    handleCollpase(event, rowKey, isOpen) {
        const { rows, perPage, page } = this.state;
        const index = (perPage * (page - 1) * 2) + rowKey; // Adjust for page set
        rows[index].isOpen = isOpen;
        this.setState({
            rows
        });
    }

    handleSort(_event, index, direction) {
        const sorted_rows = [];
        const rows = [];
        let count = 0;

        // Convert the rows into a sortable array
        for (const aci of this.state.rows) {
            if (aci.cells[1]) {
                sorted_rows.push({
                    name: aci.cells[0],
                    type: aci.cells[1],
                    fullAci: aci.fullAci,
                });
            }
        }

        // Sort the old rows and build new rows
        if (index === 1) {
            sorted_rows.sort((a, b) => (a.name > b.name) ? 1 : -1);
        } else {
            sorted_rows.sort((a, b) => (a.type > b.type) ? 1 : -1);
        }
        if (direction !== SortByDirection.asc) {
            sorted_rows.reverse();
        }
        for (const aci of sorted_rows) {
            rows.push({
                isOpen: false,
                cells: [aci.name, aci.type],
                fullAci: aci.fullAci,
            });
            rows.push({
                parent: count,
                fullWidth: true,
                cells: [aci.fullAci]
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

    handleToggleWizard = () => {
        this.setState({
            isWizardOpen: !this.state.isWizardOpen
        });
    };

    onToggleWizard = () => {
        this.setState({
            isWizardOpen: !this.state.isWizardOpen
        });
    };

    handleToggleManual = () => {
        this.setState({
            isManualOpen: !this.state.isManualOpen,
            aciTextNew: "",
            modalSpinning: false,
        });
    };

    getActionsForRow = (rowData) => {
        // Return empty array if it's the "No ACI's" row
        if (rowData.cells.length === 1 && rowData.cells[0] === _("No ACI's")) {
            return [];
        }
        
        // Only return actions for parent rows (not expanded rows)
        if (!rowData.cells || rowData.parent !== undefined) {
            return [];
        }
        
        return [
            {
                title: _("Edit ACI"),
                onClick: () => this.showEditAci(rowData)
            },
            {
                isSeparator: true
            },
            {
                title: _("Remove ACI"),
                onClick: () => this.showDeleteConfirm(rowData)
            }
        ];
    };

    prepareTableRows(tableRows) {
        const parentRows = [];
        const expandedContentMap = new Map();
        
        for (let i = 0; i < tableRows.length; i += 2) {
            const currentRow = tableRows[i];
            if (currentRow) {
                parentRows.push(currentRow);
                if (tableRows[i + 1]) {
                    expandedContentMap.set(i, tableRows[i + 1]);
                }
            }
        }
        
        return { parentRows, expandedContentMap };
    }


    render() {
        const { columns, rows, perPage, page, sortBy, showModal, actions, modalSpinning } = this.state;
        const origRows = [...rows];
        const startIdx = ((perPage * page) - perPage) * 2;
        let tableRows = origRows.splice(startIdx, perPage * 2);

        // Prepare rows without using hooks
        const { parentRows, expandedContentMap } = this.prepareTableRows(tableRows);

        let btnName = _("Save ACI");
        const extraPrimaryProps = {};
        if (modalSpinning) {
            btnName = _("Saving ACI ...");
            extraPrimaryProps.spinnerAriaValueText = _("Loading");
        }

        const title = _("Manage ACI's For ") + this.props.wizardEntryDn;

        let cols = columns;
        if (rows.length === 0) {
            tableRows = [{ cells: [_("No ACI's")] }];
            cols = [{ title: _("Access Control Instructions") }];
        }

        // Determine if we should show the actions column
        const showActions = rows.length > 0 && !(rows.length === 1 && rows[0].cells.length === 1 && rows[0].cells[0] === _("No ACI's"));
        const isEmptyTable = !showActions;

        return (
            <>
                <Modal
                    variant={ModalVariant.medium}
                    className="ds-modal-select"
                    title={title}
                    isOpen={showModal}
                    onClose={this.handleCloseModal}
                    actions={[
                        <Button
                            key="acc aci"
                            variant="primary"
                            onClick={this.handleToggleWizard}
                        >
                            {_("Add ACI Wizard")}
                        </Button>,
                        <Button
                            key="acc aci manual"
                            variant="primary"
                            onClick={this.handleToggleManual}
                        >
                            {_("Add ACI Manually")}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleCloseModal}>
                            {_("Close")}
                        </Button>
                    ]}
                >
                    <Table 
                        aria-label="Expandable ACI table"
                        variant='compact'
                    >
                        <Thead>
                            <Tr>
                                {!isEmptyTable && (
                                    <Th 
                                        screenReaderText={_("Expand/Collapse Row")}
                                    />
                                )}
                                {cols.map((column, columnIndex) => (
                                    <Th 
                                        key={columnIndex}
                                        sort={column.transforms?.includes(sortable) ? {
                                            sortBy,
                                            onSort: (_evt, index, direction) => this.handleSort(index, direction),
                                            columnIndex
                                        } : undefined}
                                    >
                                        {column.title}
                                    </Th>
                                ))}
                                {showActions && (
                                    <Th 
                                        screenReaderText={_("Actions")}
                                    />
                                )}
                            </Tr>
                        </Thead>
                        <Tbody>
                            {isEmptyTable ? (
                                <Tr>
                                    <Td>{tableRows[0].cells[0]}</Td>
                                </Tr>
                            ) : (
                                parentRows.map((row, rowIndex) => (
                                    <React.Fragment key={rowIndex}>
                                        <Tr>
                                            <Td
                                                expand={{
                                                    rowIndex,
                                                    isExpanded: row.isOpen,
                                                    onToggle: () => this.handleCollpase(null, rowIndex * 2, !row.isOpen)
                                                }}
                                            />
                                            {row.cells.map((cell, cellIndex) => (
                                                <Td key={cellIndex}>{cell}</Td>
                                            ))}
                                            <Td isActionCell>
                                                <ActionsColumn 
                                                    items={this.getActionsForRow(row)}
                                                />
                                            </Td>
                                        </Tr>
                                        {row.isOpen && expandedContentMap.has(rowIndex * 2) && (
                                            <Tr isExpanded={true}>
                                                <Td 
                                                    colSpan={cols.length + 2}
                                                    noPadding
                                                >
                                                    <ExpandableRowContent>
                                                        {expandedContentMap.get(rowIndex * 2).cells[0]}
                                                    </ExpandableRowContent>
                                                </Td>
                                            </Tr>
                                        )}
                                    </React.Fragment>
                                ))
                            )}
                        </Tbody>
                    </Table>
                    {!isEmptyTable && (
                        <Pagination
                            itemCount={rows.length / 2}
                            widgetId="pagination-options-menu-bottom"
                            perPage={perPage}
                            page={page}
                            variant="bottom"
                            onSetPage={(_evt, value) => this.handleSetPage(value)}
                            onPerPageSelect={(_evt, value) => this.handlePerPageSelect(value)}
                        />
                    )}
                    <div className="ds-margin-top-xlg" />
                    {this.state.isWizardOpen &&
                        <AddNewAci
                            wizardEntryDn={this.props.wizardEntryDn}
                            editorLdapServer={this.props.editorLdapServer}
                            onReload={this.props.onReload}
                            refreshAciTable={this.buildAciTable}
                            isWizardOpen={this.state.isWizardOpen}
                            handleToggleWizard={this.onToggleWizard}
                            setWizardOperationInfo={this.props.setWizardOperationInfo}
                            treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                            addNotification={this.props.addNotification}
                        />}
                    <DoubleConfirmModal
                        showModal={this.state.showDeleteConfirm}
                        closeHandler={this.closeDeleteConfirm}
                        handleChange={this.onChange}
                        actionHandler={this.deleteACI}
                        spinning={modalSpinning}
                        item={this.state.aciName}
                        checked={this.state.modalChecked}
                        mTitle={_("Delete ACI")}
                        mMsg={_("Are you sure you want to delete this ACI?")}
                        mSpinningMsg={_("Deleting ...")}
                        mBtnName={_("Delete ACI")}
                    />
                </Modal>
                <Modal
                    variant={ModalVariant.medium}
                    title={_("Edit ACI")}
                    isOpen={this.state.showEditAci}
                    onClose={this.handleCloseEditAci}
                    actions={[
                        <Button
                            key="acc aci"
                            variant="primary"
                            onClick={this.handleSaveACI}
                            isLoading={modalSpinning}
                            spinnerAriaValueText={modalSpinning ? _("Loading") : undefined}
                            {...extraPrimaryProps}
                            isDisabled={this.state.aciText === this.state.aciTextNew || modalSpinning}
                        >
                            {btnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleCloseEditAci}>
                            {_("Close")}
                        </Button>
                    ]}
                >
                    <TextArea
                        className="ds-textarea"
                        id="aciTextNew"
                        value={this.state.aciTextNew}
                        onChange={(e, str) => { this.onChange(e) }}
                        aria-label="aci text edit area"
                        autoResize
                        resizeOrientation="vertical"
                    />
                    <Button
                        className="ds-margin-top"
                        key="reset"
                        variant="secondary"
                        onClick={this.handleResetACIText}
                        isDisabled={this.state.aciText === this.state.aciTextNew}
                        size="sm"
                    >
                        {_("Reset ACI")}
                    </Button>
                </Modal>
                <Modal
                    variant={ModalVariant.medium}
                    title={_("Add ACI Manually")}
                    isOpen={this.state.isManualOpen}
                    onClose={this.handleToggleManual}
                    actions={[
                        <Button
                            key="acc aci manual add"
                            variant="primary"
                            onClick={this.handleAddAciManual}
                            isLoading={modalSpinning}
                            spinnerAriaValueText={modalSpinning ? _("Loading") : undefined}
                            {...extraPrimaryProps}
                            isDisabled={this.state.aciTextNew === "" || modalSpinning}
                        >
                            {btnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleToggleManual}>
                            {_("Close")}
                        </Button>
                    ]}
                >
                    <TextArea
                        className="ds-textarea"
                        id="aciTextNew"
                        value={this.state.aciTextNew}
                        onChange={(e, str) => { this.onChange(e) }}
                        aria-label="aci text edit area"
                        autoResize
                        resizeOrientation="vertical"
                        placeholder={_("Enter ACI ...")}
                    />
                </Modal>
            </>
        );
    }
}

export default AciWizard;
