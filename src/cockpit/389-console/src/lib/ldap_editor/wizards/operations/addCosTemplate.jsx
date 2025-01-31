import cockpit from "cockpit";
import React from 'react';
import {
	Alert,
	Bullseye,
	Button,
	Card,
	CardBody,
	CardTitle,
	Form,
	Grid,
	GridItem,
	NumberInput,
	Modal,
	ModalVariant,
	Pagination,
	SearchInput,
	SimpleList,
	SimpleListItem,
	Spinner,
	Text,
	TextContent,
	TextInput,
	TextList,
	TextListItem,
	TextVariants,
	Title,
	Tooltip,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	BadgeToggle,
	Dropdown,
	DropdownItem,
	DropdownPosition,
	Wizard
} from '@patternfly/react-core/deprecated';
import {
	breakWord,
	headerCol,
    Table,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
} from '@patternfly/react-table';
import {
    createLdapEntry,
    generateUniqueId,
    getSingleValuedAttributes,
} from '../../lib/utils.jsx';
import {
    InfoCircleIcon
} from '@patternfly/react-icons';
import EditableTable from '../../lib/editableTable.jsx';
import AddCosDefinition from './addCosDefinition.jsx';

const _ = cockpit.gettext;

class AddCosTemplate extends React.Component {
    constructor (props) {
        super(props);

        this.originalEntryRows = [];
        this.singleValuedAttributes = ['cn'];
        this.requiredAttributes = ['dn'];
        this.operationColumns = [
            { title: _("Statement") },
            { title: _("Attribute") },
            { title: _("Value"), cellTransforms: [breakWord] }
        ];

        this.state = {
            loading: true,
            isOCDropDownOpen: false,
            isAttrDropDownOpen: false,
            namingAttributeData: ['', ''],
            namingAttrPropsName: '',
            namingRowID: -1,
            namingAttr: "",
            namingVal: "",
            cospriority: 0,
            editableTableData: [],
            ldifArray: [],
            cleanLdifArray: [],
            validMods: false,
            commandOutput: '',
            resultVariant: 'default',
            stepIdReached: this.props.stepReached,
            itemCountOc: 0,
            pageOc: 1,
            perPageOc: 6,
            itemCountAttr: 0,
            pageAttr: 1,
            perPageAttr: 10,
            columnsAttr: [
                { title: _("Attribute Name"), cellTransforms: [headerCol()] },
                { title: _("From ObjectClass") }
            ],
            columnsOc: [
                { title: _("ObjectClass Name"), cellTransforms: [headerCol()] },
                { title: _("Required Attributes"), cellTransforms: [breakWord] },
                { title: _("Optional Attributes"), cellTransforms: [breakWord] }
            ],
            rowsOc: [],
            rowsAttr: [],
            rowsAttrOrig: [],
            rowsValues: [],
            pagedRowsOc: [],
            pagedRowsAttr: [],
            selectedObjectClasses: [],
            selectedAttributes: [],
            attrsToRemove: [],
            adding: true,
            goBackToCoSDefinition: false,
            createdTemplate: "",
            isConfirmModalOpen: false,
            createTemplateEnd: false,
            searchValue: "",
            searchAttrValue: "",
        };

        this.handleConfirmModalToggle = () => {
            this.setState(({ isConfirmModalOpen }) => ({
                isConfirmModalOpen: !isConfirmModalOpen,
            }));
        };

        this.handleCreateTemplateEnd = () => {
            this.setState({
                createTemplateEnd: true
            }, () => { this.props.onReload() });
        };

        this.handleMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            });
        };
        this.handleConfigChange = (event, id, min, max) => {
            let maxValue = this.maxValue;
            if (max !== 0) {
                maxValue = max;
            }
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > maxValue ? maxValue : newValue < min ? min : newValue
            });
        };
        this.handlePlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            });
        };

        this.handleNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            // The function updateValuesTableRows() is called upon new seletion(s)
            // Make sure the values table is updated in case no selection was made.
            if (id === 4) {
                // Just call this function in order to make sure the values table is up-to-date
                // even after navigating back and forth.
                this.updateAttributeTableRows();
            } else if (id === 5) {
                // Remove attributes from removed objectclasses
                this.cleanUpEntry();
            } else if (id === 6) {
                // Generate the LDIF data at step 5
                this.generateLdifData();
            } else if (id === 7) {
                // Create the LDAP entry.
                createLdapEntry(this.props.editorLdapServer,
                                this.state.ldifArray,
                                (result) => {
                                    const myDn = this.state.ldifArray[0].substring(4);
                                    this.setState({
                                        commandOutput: result.errorCode === 0 ? _("Successfully added entry!") : _("Failed to add entry, error: ") + result.errorCode,
                                        resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                                        adding: false,
                                        createdTemplate: myDn,
                                    }, () => { this.props.onReload() });

                                    const opInfo = {
                                        operationType: 'ADD',
                                        resultCode: result.errorCode,
                                        time: Date.now(),
                                        entryDn: myDn,
                                        relativeDn: this.state.namingAttrVal
                                    };
                                    this.props.setWizardOperationInfo(opInfo);
                                }
                );
            } else if ((id === 8) && (this.props.cosDefCreateMoreTemplate)) {
                this.setState({
                    isConfirmModalOpen: true
                });
            } else if ((id === 8) && (this.props.definitionWizardEntryDn !== '')) {
                this.setState({
                    goBackToCoSDefinition: true
                }, () => { this.props.onReload() });
            }
        };

        this.handleToggleOpenWizard = () => {
            if (this.props.definitionWizardEntryDn !== '') {
                this.setState({
                    goBackToCoSDefinition: true
                }, () => { this.props.onReload() });
            } else {
                this.props.handleToggleWizard();
            }
        };

        this.handleCreateTemplate = () => {
            this.setState({
                goBackToCoSDefinition: true
            });
        };

        this.cleanUpEntry = () => {
            const newRows = [];
            let validMods = true;
            for (const row of this.state.editableTableData) {
                const attr = row.attr.toLowerCase();
                if (this.state.attrsToRemove.indexOf(attr) === -1) {
                    if (row.val === "") {
                        validMods = false;
                    }
                    newRows.push(row);
                }
            }

            this.setState({
                editableTableData: newRows,
                validMods,
            });
        };

        this.handleOCSearchChange = (event, value) => {
            let ocRows = [];
            const allOCs = [];
            const val = value.toLowerCase();

            // Get fresh list of Objectclasses andwhat is selected
            this.props.allObjectclasses.map(oc => {
                let selected = false;
                let selectionDisabled = false;
                for (const selectedOC of this.state.selectedObjectClasses) {
                    if (selectedOC.cells[0].toLowerCase() === oc.name.toLowerCase()) {
                        selected = true;
                        break;
                    }
                }
                if (oc.name === "top") {
                    // Can not remove objectclass=top
                    selectionDisabled = true;
                } else if (oc.name === 'costemplate') {
                    selectionDisabled = true;
                    selected = true;
                }
                allOCs.push(
                    {
                        cells: [
                            oc.name,
                            oc.required.join(', '),
                            oc.optional.join(', '),
                        ],
                        selected,
                        disableSelection: selectionDisabled
                    });
                return [];
            });

            // Process search filter on the entire list
            if (value !== "") {
                for (const row of allOCs) {
                    const name = row.cells[0].toLowerCase();
                    const reqAttrs = row.cells[1].toLowerCase();
                    const optAttrs = row.cells[2].toLowerCase();
                    if (name.includes(val) || reqAttrs.includes(val) || optAttrs.includes(val)) {
                        ocRows.push(row);
                    }
                }
            } else {
                // Restore entire rowsOc list
                ocRows = allOCs;
            }

            this.setState({
                rowsOc: ocRows,
                pagedRowsOc: ocRows.slice(0, this.state.perPageOc),
                itemCountOc: ocRows.length,
                searchValue: value
            });
        };

        this.handleAttrSearchChange = (event, value) => {
            let attrRows = [];
            const allAttrs = this.state.rowsAttrOrig;
            const val = value.toLowerCase();

            // Process search filter on the entire list
            if (val !== "") {
                for (const row of allAttrs) {
                    const name = row.attributeName.toLowerCase();
                    if (name.includes(val)) {
                        attrRows.push(row);
                    }
                }
            } else {
                // Restore entire row list
                attrRows = allAttrs;
            }

            this.setState({
                rowsAttr: attrRows,
                pagedRowsAttr: attrRows.slice(0, this.state.perPageAttr),
                itemCountAttr: attrRows.length,
                searchAttrValue: value
            });
        };
        // End constructor().
    }

    isAttributeSingleValued = (attr) => {
        return this.singleValuedAttributes.includes(attr.toLowerCase());
    };

    isAttributeRequired = attr => {
        return this.requiredAttributes.includes(attr);
    };

    enableNextStep = (yes) => {
        this.setState({
            validMods: yes
        });
    };

    saveCurrentRows = (editableTableData) => {
        let validMods = true;
        let namingVal = this.state.namingVal;
        let namingAttrVal = this.state.namingAttrVal;
        for (const row of editableTableData) {
            if (row.val === "") {
                validMods = false;
                break;
            }
            if (row.id === this.state.namingRowID) {
                namingVal = row.val;
                namingAttrVal = row.attr + "=" + row.val;
            }
        }

        this.setState({
            editableTableData,
            validMods,
            namingVal,
            namingAttrVal,
        });
    };

    componentDidMount () {
        const ocArray = [];
        getSingleValuedAttributes(this.props.editorLdapServer,
                                  (myAttrs) => {
                                      this.singleValuedAttributes = [...myAttrs];
                                  });

        this.props.allObjectclasses.map(oc => {
            let selectionDisabled = false;
            let selected = false;
            if (oc.name === "top") {
                // Can not remove objectclass=top
                selectionDisabled = true;
            }
            if (oc.name === 'costemplate') {
                selectionDisabled = true;
                selected = true;
                // For costemplate, make all of the attribute
                ocArray.push(
                    {
                        cells: [
                            oc.name,
                            oc.required.concat(oc.optional).join(', '),
                            ''
                        ],
                        selected,
                        disableSelection: selectionDisabled
                    });
                const selectedOC = [...this.state.selectedObjectClasses];
                selectedOC.push(ocArray[ocArray.length - 1]);
                this.setState({
                    selectedObjectClasses: selectedOC,
                });
            } else {
                ocArray.push(
                    {
                        cells: [
                            oc.name,
                            oc.required.join(', '),
                            oc.optional.join(', '),
                        ],
                        selected,
                        disableSelection: selectionDisabled
                    });
            }
            return [];
        });

        this.setState({
            itemCountOc: ocArray.length,
            rowsOc: ocArray,
            pagedRowsOc: ocArray.slice(0, this.state.perPageOc),
            loading: false,
        });
    }

    handleSetPageOc = (_event, pageNumber) => {
        this.setState({
            pageOc: pageNumber,
            pagedRowsOc: this.getItemsToShow(pageNumber, this.state.perPageOc, 'ObjectClassTable')
        });
    };

    handleSetPageAttr = (_event, pageNumber) => {
        this.setState({
            pageAttr: pageNumber,
            pagedRowsAttr: this.getItemsToShow(pageNumber, this.state.perPageAttr, 'AttributeTable')
        });
    };

    handlePerPageSelectOc = (_event, perPage) => {
        this.setState({
            pageOc: 1,
            perPageOc: perPage,
            pagedRowsOc: this.getItemsToShow(1, perPage, 'ObjectClassTable')
        });
    };

    handlePerPageSelectAttr = (_event, perPage) => {
        this.setState({
            pageAttr: 1,
            perPageAttr: perPage,
            pagedRowsAttr: this.getItemsToShow(1, perPage, 'AttributeTable')
        });
    };

    getItemsToShow (page, perPage, option) {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        const newRows = option === 'ObjectClassTable'
            ? this.state.rowsOc.slice(start, end)
            : option === 'AttributeTable'
                ? this.state.rowsAttr.slice(start, end)
                : [];
        return newRows;
    }

    handleSelectOc = (event, isSelected, rowId) => {
        // Process only the entries in the current page ( pagedRowsOc )
        const rows = [...this.state.pagedRowsOc];
        rows[rowId].selected = isSelected;
        // Find the entry in the full array and set 'isAttributeSelected' accordingly
        // The property 'selected' is used to build the attribute table.
        // The row ID cannot be used since it changes with the pagination.
        const ocName = this.state.pagedRowsOc[rowId].cells[0];
        const allItems = [...this.state.rowsOc];
        const index = allItems.findIndex(item => item.cells[0] === ocName);
        allItems[index].selected = isSelected;

        let selectedObjectClasses = [...this.state.selectedObjectClasses];
        if (isSelected) {
            // Add to selected OC
            selectedObjectClasses.push(allItems[index]);
        } else {
            // Remove OC from selected list
            selectedObjectClasses = selectedObjectClasses.filter(row => (row.cells[0] !== allItems[index].cells[0]));
        }

        const attrsToRemove = [];
        if (!isSelected) {
            // Removing an objectclass, this will impact the entry as we might have to remove attributes
            let ocAttrs = allItems[index].cells[1].toLowerCase().replace(/\s/g, '')
                    .split(',');
            ocAttrs = ocAttrs.concat(allItems[index].cells[2].toLowerCase().replace(/\s/g, '')
                    .split(','));
            let currAttrs = [];
            for (const oc of selectedObjectClasses) {
                // Gather all the allowed attributes
                currAttrs = currAttrs.concat(oc.cells[1].toLowerCase().replace(/\s/g, '')
                        .split(','));
                currAttrs = currAttrs.concat(oc.cells[2].toLowerCase().replace(/\s/g, '')
                        .split(','));
            }

            for (const attr of ocAttrs) {
                if (currAttrs.indexOf(attr) === -1) {
                    // No other OC allows this attribute, it must go
                    attrsToRemove.push(attr);
                }
            }
        }

        this.setState({
            rowsOc: allItems,
            pagedRowsOc: rows,
            selectedObjectClasses,
            attrsToRemove,
        }, () => {
            this.updateAttributeTableRows();
        });
    };

    handleSelectAttr = (event, isSelected, rowId) => {
        let newEditableData = this.state.editableTableData;
        const rows = [...this.state.pagedRowsAttr];

        // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
        if (this.state.pagedRowsAttr[rowId].disableCheckbox === true) {
            return;
        } // End hack.

        // Process only the entries in the current page ( pagedRowsAttr )
        rows[rowId].selected = isSelected;

        // Find the entry in the full array and set 'isAttributeSelected' accordingly
        // The property 'isAttributeSelected' is used to build the LDAP entry to add.
        // The row ID cannot be used since it changes with the pagination.
        const attrName = rows[rowId].cells[0];
        const allItems = [...this.state.rowsAttr];
        const index = allItems.findIndex(item => item.cells[0] === attrName);
        allItems[index].isAttributeSelected = isSelected;
        const selectedAttributes = allItems
                .filter(item => item.isAttributeSelected)
                .map(attrObj => [attrObj.attributeName, attrObj.cells[1]]);

        // Update the table rows as needed
        const rowAttr = rows[rowId].attributeName.toLowerCase();
        const found = this.state.editableTableData.filter(item => (item.attr.toLowerCase() === rowAttr));
        if (isSelected) {
            if (found.length === 0 && rowAttr !== 'objectclass') {
                const obj = {};
                obj.id = generateUniqueId();
                obj.attr = rows[rowId].attributeName;
                obj.val = "";
                obj.namingAttr = false;
                obj.required = false;
                newEditableData = [...newEditableData, obj];
            }
        } else if (found.length > 0) {
            // Remove the row if present
            newEditableData = this.state.editableTableData.filter(item => (item.attr.toLowerCase() !== rowAttr));
        }

        let validMods = true;
        for (const row of newEditableData) {
            if (row.val === "") {
                validMods = false;
            }
        }

        this.setState({
            rowsAttr: allItems,
            pagedRowsAttr: rows,
            editableTableData: newEditableData,
            selectedAttributes,
            validMods
        });
    };

    updateAttributeTableRows = () => {
        const ocToProcess = [...this.state.selectedObjectClasses];
        const rowsAttr = [];
        const attrList = [];
        let namingRowID = this.state.namingRowID;
        let namingAttr = this.state.namingAttr;
        const namingVal = this.state.namingVal;
        const cospriority = this.state.cospriority;

        for (const oc of ocToProcess) {
            // Rebuild the attribute arrays.
            const required = oc.cells[1].split(',');
            const optional = oc.cells[2].split(',');

            for (let attr of required) {
                attr = attr.trim().toLowerCase();
                if (attr === '') {
                    continue;
                }

                if (!attrList.includes(attr)) {
                    attrList.push(attr);
                    rowsAttr.push({
                        selected: true,
                        disableCheckbox: true, // TODO: Hack until upgrading!
                        isAttributeSelected: true,
                        attributeName: attr,
                        cells: [{
                            title: (
                                <>
                                    <strong>{attr}</strong>
                                </>
                            )
                        },
                        oc.cells[0]]
                    });
                }

                // Loop over entry attributes and add the attribute, with an
                // empty value, to the editableTableData
                const found = this.state.editableTableData.filter(item => (item.attr.toLowerCase() === attr));
                if (found.length === 0 && attr !== 'objectclass') {
                    const new_id = generateUniqueId();
                    if (namingRowID === -1) {
                        namingRowID = new_id;
                        namingAttr = attr;
                    }

                    const obj = {};
                    obj.id = new_id;
                    obj.attr = attr;
                    if (attr.toLowerCase() === 'cn') {
                        obj.val = namingVal;
                    } else if (attr.toLowerCase() === 'cospriority') {
                        obj.val = cospriority;
                    } else {
                        obj.val = '';
                    }
                    obj.namingAttr = false;
                    obj.required = true;

                    this.setState(prevState => ({
                        editableTableData: [...prevState.editableTableData, obj],
                        validMods: false,
                        namingRowID,
                        namingAttr,
                    }));
                }
            }

            for (let attr of optional) {
                attr = attr.trim();
                if (attr === '') {
                    continue;
                }
                if (!attrList.includes(attr)) {
                    let selected = false;
                    for (const existingRow of this.state.editableTableData) {
                        if (existingRow.attr.toLowerCase() === attr.toLowerCase()) {
                            selected = true;
                            break;
                        }
                    }

                    attrList.push(attr);
                    rowsAttr.push({
                        attributeName: attr,
                        isAttributeSelected: selected,
                        selected,
                        cells: [attr, oc.cells[0]]
                    });
                }
            }
        }

        // Update the rows where user can select the attributes.
        rowsAttr.sort((a, b) => (a.attributeName > b.attributeName) ? 1 : -1);
        this.setState({
            rowsAttr,
            rowsAttrOrig: [...rowsAttr],
            selectedAttributes: rowsAttr.filter(item => item.isAttributeSelected)
                    .map(attrObj => [attrObj.attributeName, attrObj.cells[1]]),
            itemCountAttr: rowsAttr.length,
        }, () => {
            // getItemsToShow() expects rowAttrs to be updated already, so we
            // have to do this callback
            this.setState({
                pagedRowsAttr: this.getItemsToShow(this.state.pageAttr, this.state.perPageAttr,
                                                   'AttributeTable')
            });
        });
    };

    generateLdifData = () => {
        const objectClassData = ['top'];
        const attribute = this.state.namingAttr;
        const value = this.state.namingVal;
        const valueData = [];

        for (const oc of this.state.selectedObjectClasses) {
            if (oc && !objectClassData.includes(oc.cells[0])) {
                objectClassData.push(oc.cells[0]);
            }
        }
        for (const item of this.state.editableTableData) {
            const attrName = item.attr;
            valueData.push(`${attrName}: ${item.val}`);
        }

        const ocArray = objectClassData.map(oc => `ObjectClass: ${oc}`);
        const dnLine = `dn: ${attribute}=${value},${this.props.wizardEntryDn}`;
        const ldifArray = [
            dnLine,
            ...ocArray,
            ...valueData
        ];

        // Hide userpassword value
        const cleanLdifArray = [...ldifArray];
        for (const idx in cleanLdifArray) {
            if (cleanLdifArray[idx].toLowerCase().startsWith("userpassword")) {
                cleanLdifArray[idx] = "userpassword: ********";
                break;
            }
        }

        this.setState({
            ldifArray,
            cleanLdifArray
        });
    };

    handleOCDropDownToggle = isOpen => {
        this.setState({
            isOCDropDownOpen: isOpen
        });
    };

    handleOCDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isOCDropDownOpen: !prevState.isOCDropDownOpen };
        });
    };

    buildOCDropdown = () => {
        const { isOCDropDownOpen, selectedObjectClasses } = this.state;
        const numSelected = this.state.rowsOc.filter(item => item.selected).length;
        const items = selectedObjectClasses.map((oc) =>
            <DropdownItem key={oc.cells[0]}>{oc.cells[0]}</DropdownItem>
        );

        return (
            <Dropdown
                className="ds-dropdown-padding"
                onSelect={this.handleOCDropDownSelect}
                position={DropdownPosition.left}
                toggle={
                    <BadgeToggle id="toggle-oc-select" onToggle={(_event, isOpen) => this.handleOCDropDownToggle(isOpen)}>
                        {numSelected !== 0 ? <>{numSelected} {_("selected")} </> : <>0 {_("selected")} </>}
                    </BadgeToggle>
                }
                isOpen={isOCDropDownOpen}
                dropdownItems={items}
            />
        );
    };

    handleAttrDropDownToggle = isOpen => {
        this.setState({
            isAttrDropDownOpen: isOpen
        });
    };

    handleAttrDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isAttrDropDownOpen: !prevState.isAttrDropDownOpen };
        });
    };

    buildAttrDropdown = () => {
        const { isAttrDropDownOpen, selectedAttributes } = this.state;
        const numSelected = selectedAttributes.length;
        const items = selectedAttributes.map((attr) =>
            <DropdownItem key={attr[0]}>{attr[0]}</DropdownItem>
        );

        return (
            <Dropdown
                className="ds-dropdown-padding"
                onSelect={this.handleAttrDropDownSelect}
                position={DropdownPosition.left}
                toggle={
                    <BadgeToggle id="toggle-attr-select" onToggle={(_event, isOpen) => this.handleAttrDropDownToggle(isOpen)}>
                        {numSelected !== 0 ? <>{numSelected} {_("selected")} </> : <>0 {_("selected")} </>}
                    </BadgeToggle>
                }
                isOpen={isAttrDropDownOpen}
                dropdownItems={items}
            />
        );
    };

    setNamingRowID = (namingRowID) => {
        let namingAttrVal = "";
        let namingAttr = "";
        let namingVal = "";
        const rows = this.state.editableTableData;

        for (const row of rows) {
            if (row.id === namingRowID) {
                namingAttrVal = row.attr + "=" + row.val;
                namingAttr = row.attr;
                namingVal = row.val;
                break;
            }
        }

        this.setState({
            namingRowID,
            namingAttrVal,
            namingAttr,
            namingVal,
        });
    };

    handleChange (e) {
        const attr = e.target.id;
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [attr]: value,
        });
    }

    onToggleWizard () {
        this.props.handleToggleWizard();
    }

    render () {
        const {
            loading, itemCountOc, pageOc, perPageOc, columnsOc, pagedRowsOc,
            itemCountAttr, pageAttr, perPageAttr, columnsAttr, pagedRowsAttr,
            commandOutput, namingAttr, namingVal, cospriority, stepIdReached,
            ldifArray, resultVariant, editableTableData, validMods, cleanLdifArray,
            selectedAttributes, selectedObjectClasses, goBackToCoSDefinition, createTemplateEnd,
            isOCDropDownOpen, isAttrDropDownOpen
        } = this.state;

        if (createTemplateEnd) {
            return (
                <AddCosTemplate
                    isWizardOpen={this.props.isWizardOpen}
                    handleToggleWizard={this.opToggleWizard}
                    wizardEntryDn={this.props.wizardEntryDn}
                    editorLdapServer={this.props.editorLdapServer}
                    setWizardOperationInfo={this.props.setWizardOperationInfo}
                    firstStep={this.props.firstStep}
                    onReload={this.props.onReload}
                    allObjectclasses={this.props.allObjectclasses}
                    treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                    stepReached={2}
                    cosDefCreateMoreTemplate
                />
            );
        } else if (goBackToCoSDefinition) {
            let myDn = this.state.createdTemplate;
            if (resultVariant === 'danger') {
                myDn = "";
            }
            return (
                <AddCosDefinition
                    isWizardOpen={this.props.isWizardOpen}
                    handleToggleWizard={this.onToggleWizard}
                    wizardEntryDn={this.props.definitionWizardEntryDn}
                    editorLdapServer={this.props.editorLdapServer}
                    setWizardOperationInfo={this.props.setWizardOperationInfo}
                    firstStep={this.props.firstStep}
                    onReload={this.props.onReload}
                    allObjectclasses={this.props.allObjectclasses}
                    treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                    createdTemplateDN={myDn}
                    stepReached={2}
                    cosDefName={this.props.cosDefName}
                    cosDefDesc={this.props.cosDefDesc}
                    cosDefType={this.props.cosDefType}
                />
            );
        }

        const namingValAndPriority = (
            <div>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>{_("Select Name And CoS Priority")}</Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={12}>
                        <TextContent className="ds-margin-top">
                            <Text>
                                {_("The CoS template entry contains the value or values of the attributes generated by the CoS logic. The CoS template entry contains a general object class of cosTemplate. The CoS template entries for a given CoS are stored in the directory tree along with the CoS definition.")}
                            </Text>
                            <Text>
                                {_("The relative distinguished name (RDN) of the template entry is determined by one of the following:")}
                            </Text>
                            <TextList>
                                <TextListItem>
                                    {_("The DN of the template entry alone. This type of template is associated with a pointer CoS definition.")}
                                </TextListItem>
                                <TextListItem>
                                    {_("The value of one of the target entry's attributes. The attribute used to provide the relative DN to the template entry is specified in the CoS definition entry using the cosIndirectSpecifier attribute. This type of template is associated with an indirect CoS definition.")}
                                </TextListItem>
                                <TextListItem>
                                    {_("By a combination of the DN of the subtree where the CoS performs a one level search for templates and the value of one of the target entry's attributes. This type of template is associated with a classic CoS definition.")}
                                </TextListItem>
                            </TextList>
                            <Text>
                                {_("Please, consult official documentation for more information and examplesa.")}
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                <Form autoComplete="off">
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={3} className="ds-label">
                            <TextContent>
                                <Text>
                                    {_("CoS Template Name")}
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={namingVal}
                                type="text"
                                id="namingVal"
                                aria-describedby="namingVal"
                                name="namingVal"
                                onChange={(e, str) => {
                                    this.handleChange(e);
                                }}
                                validated={this.state.namingVal === '' ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            <TextContent>
                                <Text>{_("CoS Priority")}
                                    <Tooltip
                                        position="bottom"
                                        content={
                                            <div>
                                                {_("Specifies which template provides the attribute value when CoS templates compete to provide an attribute value. This attribute represents the global priority of a template. A priority of zero is the highest priority.")}
                                            </div>
                                        }
                                    >
                                        <a className="ds-font-size-md">
                                            <InfoCircleIcon className="ds-info-icon" />
                                        </a>
                                    </Tooltip>
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={cospriority}
                                min={0}
                                max={2147483647}
                                onMinus={() => { this.handleMinusConfig("cospriority") }}
                                onChange={(e) => { this.handleConfigChange(e, "cospriority", 0, 2147483647) }}
                                onPlus={() => { this.handlePlusConfig("cospriority") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={8}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </div>
        );

        const objectClassStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Select ObjectClasses")}
                        </Text>
                    </TextContent>
                    <Dropdown
                        className="ds-dropdown-padding"
                        position="left"
                        onSelect={this.handleOCDropDownSelect}
                        toggle={
                            <BadgeToggle 
                                id="toggle-oc-select" 
                                badgeProps={{
                                    className: selectedObjectClasses.length > 0 ? "ds-badge-bgcolor" : undefined,
                                    isRead: selectedObjectClasses.length === 0
                                }}
                                onToggle={(_event, isOpen) => this.handleOCDropDownToggle(isOpen)}
                            >
                                {selectedObjectClasses.length > 0 ? 
                                    `${selectedObjectClasses.length} ${_("selected")}` : 
                                    `0 ${_("selected")}`}
                            </BadgeToggle>
                        }
                        isOpen={isOCDropDownOpen}
                        dropdownItems={selectedObjectClasses.map((oc) =>
                            <DropdownItem key={oc.cells[0]}>{oc.cells[0]}</DropdownItem>
                        )}
                    />
                </div>
                { loading ? (
                    <Bullseye className="ds-margin-top-xlg">
                        <Title headingLevel="h3" size="lg">
                            {_("Loading ObjectClasses ...")}
                        </Title>
                        <Spinner className="ds-center" size="lg" />
                    </Bullseye>
                ) : (
                    <div>
                        <Grid className="ds-margin-top-lg">
                            <GridItem span={5}>
                                <SearchInput
                                    className="ds-font-size-md"
                                    placeholder={_("Search Objectclasses")}
                                    value={this.state.searchValue}
                                    onChange={this.handleOCSearchChange}
                                    onClear={(evt) => this.handleOCSearchChange(evt, '')}
                                />
                            </GridItem>
                            <GridItem span={7}>
                                <Pagination
                                    itemCount={itemCountOc}
                                    page={pageOc}
                                    perPage={perPageOc}
                                    onSetPage={this.handleSetPageOc}
                                    widgetId="pagination-step-objectclass"
                                    onPerPageSelect={this.handlePerPageSelectOc}
                                    isCompact
                                />
                            </GridItem>
                        </Grid>
                        <Table aria-label="Objectclasses Table" variant="compact">
                            <Thead>
                                <Tr>
                                    <Th screenReaderText="Selection column" />
                                    {columnsOc.map((column, columnIndex) => (
                                        <Th key={columnIndex}>
                                            {typeof column === 'object' ? column.title : column}
                                        </Th>
                                    ))}
                                </Tr>
                            </Thead>
                            <Tbody>
                                {pagedRowsOc.map((row, rowIndex) => (
                                    <Tr key={rowIndex}>
                                        <Td
                                            select={{
                                                rowIndex,
                                                onSelect: this.handleSelectOc,
                                                isSelected: row.selected,
                                                isDisabled: row.disableSelection
                                            }}
                                        />
                                        {row.cells.map((cell, cellIndex) => (
                                            <Td 
                                                key={`${rowIndex}_${cellIndex}`}
                                                dataLabel={columnsOc[cellIndex]?.title || columnsOc[cellIndex]}
                                            >
                                                {typeof cell === 'object' ? cell.title : cell}
                                            </Td>
                                        ))}
                                    </Tr>
                                ))}
                            </Tbody>
                        </Table>
                    </div>
                )}
            </>
        );

        const attributeStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Select Attributes")}
                        </Text>
                    </TextContent>
                    <Dropdown
                        className="ds-dropdown-padding"
                        position="left"
                        onSelect={this.handleAttrDropDownSelect}
                        toggle={
                            <BadgeToggle 
                                id="toggle-attr-select" 
                                badgeProps={{
                                    className: selectedAttributes.length > 0 ? "ds-badge-bgcolor" : undefined,
                                    isRead: selectedAttributes.length === 0
                                }}
                                onToggle={(_event, isOpen) => this.handleAttrDropDownToggle(isOpen)}
                            >
                                {selectedAttributes.length > 0 ? 
                                    `${selectedAttributes.length} ${_("selected")}` : 
                                    `0 ${_("selected")}`}
                            </BadgeToggle>
                        }
                        isOpen={isAttrDropDownOpen}
                        dropdownItems={selectedAttributes.map((attr) =>
                            <DropdownItem key={attr[0]}>{attr[0]}</DropdownItem>
                        )}
                    />
                </div>
                <Grid className="ds-margin-top-lg">
                    <GridItem span={5}>
                        <SearchInput
                            className="ds-font-size-md"
                            placeholder={_("Search Attributes")}
                            value={this.state.searchAttrValue}
                            onChange={this.handleAttrSearchChange}
                            onClear={(evt) => this.handleAttrSearchChange(evt, '')}
                        />
                    </GridItem>
                    <GridItem span={7}>
                        <Pagination
                            itemCount={itemCountAttr}
                            page={pageAttr}
                            perPage={perPageAttr}
                            onSetPage={this.handleSetPageAttr}
                            widgetId="pagination-step-attributes"
                            onPerPageSelect={this.handlePerPageSelectAttr}
                            isCompact
                        />
                    </GridItem>
                </Grid>
                <Table aria-label="Attributes Table" variant="compact">
                    <Thead>
                        <Tr>
                            <Th screenReaderText="Selection column" />
                            {columnsAttr.map((column, columnIndex) => (
                                <Th key={columnIndex}>
                                    {typeof column === 'object' ? column.title : column}
                                </Th>
                            ))}
                        </Tr>
                    </Thead>
                    <Tbody>
                        {pagedRowsAttr.map((row, rowIndex) => (
                            <Tr key={rowIndex}>
                                <Td
                                    select={{
                                        rowIndex,
                                        onSelect: this.handleSelectAttr,
                                        isSelected: row.selected,
                                        isDisabled: row.disableCheckbox
                                    }}
                                />
                                {row.cells.map((cell, cellIndex) => (
                                    <Td 
                                        key={`${rowIndex}_${cellIndex}`}
                                        dataLabel={columnsAttr[cellIndex]?.title || columnsAttr[cellIndex]}
                                    >
                                        {typeof cell === 'object' ? cell.title : cell}
                                    </Td>
                                ))}
                            </Tr>
                        ))}
                    </Tbody>
                </Table>
            </>
        );

        const myTitle = _("DN ( Distinguished Name )");
        const entryValuesStep = (
            <>
                <Alert
                    variant={namingAttr === '' || namingVal === ''
                        ? "warning"
                        : "success"}
                    isInline
                    title={myTitle}
                >
                    <b>{_("Entry DN:")}&nbsp;&nbsp;&nbsp;</b>{(namingAttr || "??????")}={namingVal || "??????"},{this.props.wizardEntryDn}
                </Alert>
                <TextContent className="ds-margin-top">
                    <Text component={TextVariants.h3}>
                        {_("Set Attribute Values")}
                    </Text>
                </TextContent>
                <EditableTable
                    key={editableTableData}
                    wizardEntryDn={this.props.wizardEntryDn}
                    editableTableData={editableTableData}
                    isAttributeSingleValued={this.isAttributeSingleValued}
                    isAttributeRequired={this.isAttributeRequired}
                    enableNextStep={this.enableNextStep}
                    saveCurrentRows={this.saveCurrentRows}
                    allObjectclasses={this.props.allObjectclasses}
                    namingRowID={this.state.namingRowID}
                    namingAttr={this.state.namingAttr}
                    setNamingRowID={this.setNamingRowID}
                />
            </>
        );

        const ldifListItems = cleanLdifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const ldifStatementsStep = (
            <div>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant="info"
                        isInline
                        title={_("LDIF Statements")}
                    />
                </div>
                <Card isSelectable>
                    <CardBody>
                        { (ldifListItems.length > 0) &&
                            <SimpleList aria-label="LDIF data User">
                                {ldifListItems}
                            </SimpleList>}
                    </CardBody>
                </Card>
            </div>
        );

        let nb = -1;
        const ldifLines = ldifArray.map(line => {
            nb++;
            return { data: line, id: nb };
        });

        const entryReviewStep = (
            <div>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant={resultVariant}
                        isInline
                        title={_("Result for Entry Modification")}
                    >
                        {commandOutput}
                        {this.state.adding &&
                            <div>
                                <Spinner className="ds-left-margin" size="md" />
                                &nbsp;&nbsp;{_("Adding entry ...")}
                            </div>}
                    </Alert>
                </div>
                {resultVariant === 'danger' &&
                    <Card isSelectable>
                        <CardTitle>{_("LDIF Data")}</CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>}
                <Modal
                    variant={ModalVariant.small}
                    title={_("Create CoS Template")}
                    isOpen={this.state.isConfirmModalOpen}
                    onClose={this.handleConfirmModalToggle}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={this.handleCreateTemplateEnd}>
                            {_("Create Template")}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleConfirmModalToggle}>
                            {_("Cancel")}
                        </Button>
                    ]}
                >
                    <Grid>
                        <GridItem span={12}>
                            <TextContent className="ds-margin-top">
                                <Text>
                                    {cockpit.format(_("You've chosen 'Classic' CoS type. cosTemplateDN attribute is set to $0."), this.props.wizardEntryDn)}
                                </Text>
                                <Text>
                                    {_("Do you want to create a CoS template now?")}
                                </Text>
                                <Text>
                                    {cockpit.format(_("It will be added as a child to this entry: $0"), this.props.wizardEntryDn)}
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                </Modal>
            </div>
        );

        let finishButtonName;
        if (this.props.cosDefCreateMoreTemplate) {
            finishButtonName = _("Create More Templates");
        } else if (this.props.definitionWizardEntryDn !== '') {
            finishButtonName = _("Back to CoS Definition");
        } else {
            finishButtonName = _("Finish");
        }

        const addEntrySteps = [
            ...((this.props.definitionWizardEntryDn !== '') || (this.props.cosDefCreateMoreTemplate)
                ? []
                : [
                    {
                        id: 1,
                        name: this.props.firstStep[0].name,
                        component: this.props.firstStep[0].component,
                        canJumpTo: stepIdReached >= 1 && stepIdReached < 8,
                        hideBackButton: true
                    },
                ]),
            {
                id: 2,
                name: _("Select Name & Priority"),
                component: namingValAndPriority,
                enableNext: namingVal !== '',
                canJumpTo: stepIdReached >= 2 && stepIdReached < 8,
            },
            {
                id: 3,
                name: _("Select ObjectClasses"),
                component: objectClassStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 8,
                enableNext: selectedObjectClasses.length > 1,
            },
            {
                id: 4,
                name: _("Select Attributes"),
                component: attributeStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 8,
                enableNext: selectedAttributes.length > 0,
            },
            {
                id: 5,
                name: _("Edit Values"),
                component: entryValuesStep,
                canJumpTo: stepIdReached >= 5 && stepIdReached < 8,
                enableNext: validMods
            },
            {
                id: 6,
                name: _("LDIF Statements"),
                component: ldifStatementsStep,
                nextButtonText: _("Create Entry"),
                canJumpTo: stepIdReached >= 6 && stepIdReached < 8
            },
            {
                id: 7,
                name: _("Review Result"),
                component: entryReviewStep,
                nextButtonText: finishButtonName,
                canJumpTo: stepIdReached >= 7 && stepIdReached < 8,
                hideBackButton: true,
                enableNext: !this.state.adding,
            },
            ...((this.props.definitionWizardEntryDn !== '') || (this.props.cosDefCreateMoreTemplate)
                ? [
                    {
                        id: 8,
                        name: this.props.cosDefCreateMoreTemplate ? _("Create More Templates") : _("Back to CoS Definition"),
                        component: entryReviewStep,
                        nextButtonText: _("Finish"),
                        canJumpTo: stepIdReached >= 7 && stepIdReached < 8,
                        hideBackButton: true,
                    },
                ]
                : []),
        ];

        const title = (
            <>
                {_("Parent DN:")} &nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
            </>
        );

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.handleToggleOpenWizard}
                steps={addEntrySteps}
                title={_("Add CoS Template")}
                description={title}
                onNext={this.handleNext}
            />
        );
    }
}

export default AddCosTemplate;
