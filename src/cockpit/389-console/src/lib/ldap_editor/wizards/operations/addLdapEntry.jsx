import React from 'react';
import {
    Alert,
    Bullseye,
    Button,
    Card, CardHeader, CardBody, CardFooter, CardTitle,
    Dropdown, DropdownItem, DropdownPosition, DropdownToggle, DropdownToggleCheckbox,
    EmptyState, EmptyStateBody, EmptyStateVariant, EmptyStateIcon,
    Form, FormGroup,
    Pagination,
    Popover,
    SimpleList, SimpleListItem,
    Spinner,
    Text, TextArea, TextContent, TextVariants,
    Title,
    Toolbar, ToolbarGroup, ToolbarItem,
    Wizard,
} from '@patternfly/react-core';
import {
    EditableTextCell,
    EditableSelectInputCell,
    Table, TableHeader, TableBody, TableVariant,
    applyCellEdits,
    cancelCellEdits,
    breakWord,
    getErrorTextByValidator,
    headerCol,
    nowrap,
    validateCellEdits,
} from '@patternfly/react-table';
import {
    createLdapEntry,
    getAllObjectClasses,
    getSingleValuedAttributes
} from '../../lib/utils.jsx';

class AddLdapEntry extends React.Component {
    constructor (props) {
        super(props);

        this.attributeValidationRules = [
            {
                name: 'required',
                validator: value => value.trim() !== '',
                errorText: 'This field is required'
            }
        ];

        this.singleValuedAttributes = [];
        this.allObjectClasses = [];

        this.state = {
            loading: true,
            isDropDownOpen: false,
            namingAttributeData: ['', ''],
            namingAttrPropsName: '',
            namingRowIndex: -1,
            namingAttribute: '',
            namingValue: '',
            ldifArray: [],
            commandOutput: '',
            resultVariant: 'default',
            allAttributesSelected: false,
            allObjectClassesSelected: false,
            stepIdReached: 1,
            itemCountOc: 0,
            pageOc: 1,
            perPageOc: 10,
            itemCountAttr: 0,
            pageAttr: 1,
            perPageAttr: 10,
            columnsAttr: [
                { title: 'Attribute Name', cellTransforms: [headerCol()] },
                { title: 'From ObjectClass' }
            ],
            columnsOc: [
                { title: 'ObjectClass Name', cellTransforms: [headerCol()] },
                { title: 'Required Attributes', cellTransforms: [breakWord] },
                { title: 'Optional Attributes', cellTransforms: [breakWord] }
            ],
            rowsOc: [],
            rowsAttr: [],
            rowsValues: [],
            pagedRowsOc: [],
            pagedRowsAttr: [],
            selectedObjectClasses: [],
            selectedAttributes: [],
            // Values
            noEmptyValue: false,
            columnsValue: [
                'Attribute',
                'Value'
            ],
            // Review step
            reviewValue: '',
            reviewInvalidText: 'Invalid LDIF',
            reviewIsValid: true,
            reviewValidated: 'default'
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            // The function updateValueTableRows() is called upon new seletion(s)
            // Make sure the values table is updated in case no selection was made.
            if (id === 3) {
                // Just call this function in order to make sure the values table is up-to-date
                // even after navigating back and forth.
                this.updateAttributeTableRows();
            } else if (id === 4) {
                // Just call this function in order to make sure the values table is up-to-date
                // even after navigating back and forth.
                this.updateValueTableRows();
            } else if (id === 5) {
                // Generate the LDIF data at step 4.
                this.generateLdifData();
            } else if (id === 6) {
                // Create the LDAP entry.
                createLdapEntry(this.props.editorLdapServer,
                    this.state.ldifArray,
                    (result) => {
                        this.setState({
                            commandOutput: result.output,
                            resultVariant: result.errorCode === 0 ? 'info' : 'danger'
                        }, () => { this.props.onReload() });
                    }
                );
            }
        };
        // End constructor().
    }

    componentDidMount () {
        console.log('In AddLdapEntry');
        // Calling the functions in this order to avoid any possible
        // discrepancies when other components are using
        // these state data.
        // This order makes sure that the needed schema information
        // ( all objectClasses and the single attributes ) are collected
        // prior to enable the wizard steps.
        const ocArray = [];
        getSingleValuedAttributes(this.props.editorLdapServer, // TODO: Remove the getSingleValuedAttributes() part once the EditableTable class is ready!
            (myAttrs) => {
                // console.log('singleValuedAttrList = ' + singleAttrs);
                this.singleValuedAttributes = [...myAttrs];
                // Now get all the objectClasses

                getAllObjectClasses(this.props.editorLdapServer,
                    (allOcs) => {
                        this.allObjectClasses = [...allOcs];
                        // Now generate the objectClass table items.
                        this.allObjectClasses.map(oc => {
                            ocArray.push(
                                {
                                    // TODO: Optimize ==> Use cellFormatters!
                                    // required: oc.required,
                                    // optional: oc.optional,
                                    cells: [
                                        oc.name,
                                        oc.required.join(', '),
                                        oc.optional.join(', ')
                                    ]
                                });
                        });
                        console.log(`ocArray.length = ${ocArray.length}`);
                        this.setState({
                            itemCountOc: ocArray.length,
                            rowsOc: ocArray,
                            pagedRowsOc: ocArray.slice(0, this.state.perPageOc),
                            loading: false
                        });
                });
        });
    }

    onSetPageOc = (_event, pageNumber) => {
        this.setState({
            pageOc: pageNumber,
            pagedRowsOc: this.getItemsToShow(pageNumber, this.state.perPageOc, 'ObjectClassTable')
        });
    };

    onSetPageAttr = (_event, pageNumber) => {
        this.setState({
            pageAttr: pageNumber,
            pagedRowsAttr: this.getItemsToShow(pageNumber, this.state.perPageAttr, 'AttributeTable')
        });
    };

    onPerPageSelectOc = (_event, perPage) => {
        this.setState({
            pageOc: 1,
            perPageOc: perPage,
            pagedRowsOc: this.getItemsToShow(1, perPage, 'ObjectClassTable')
        });
    };

    onPerPageSelectAttr = (_event, perPage) => {
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

    isAttributeSingleValued = attributeName => {
        return this.singleValuedAttributes.includes(attributeName.toLowerCase());
    };

    onSelectOc = (event, isSelected, rowId) => {
        // const allSelected = (rowId === -1) && isSelected;
        // Process only the entries in the current page ( pagedRowsOc )
        const rows = [...this.state.pagedRowsOc];
        rows[rowId].selected = isSelected;
        // Find the entry in the full array and set 'isAttributeSelected' accordingly
        // The property 'isOcSelected' is used to build the attribute table.
        // The row ID cannot be used since it changes with the pagination.
        const ocName = this.state.pagedRowsOc[rowId].cells[0];
        const allItems = [...this.state.rowsOc];
        const index = allItems.findIndex(item => item.cells[0] === ocName);
        allItems[index].isOcSelected = isSelected;
        const selectedObjectClasses = allItems
            .filter(item => item.isOcSelected);
            // .map(selectedOc => selectedOc.cells[0]);
        this.setState({
            rowsOc: allItems,
            pagedRowsOc: rows,
            selectedObjectClasses,
            allObjectClassesSelected: allItems.length === selectedObjectClasses.length // || allSelected
        }, () => {
            this.updateAttributeTableRows();
        });
    };

    onSelectAttr = (event, isSelected, rowId) => {
        let rows;
        if (rowId === -1) {
            // Process the full table entries ( rowsAttr )
            rows = this.state.rowsAttr.map(oneRow => {
                if (!oneRow.disableCheckbox) { // Do not touch the required attributes.
                    oneRow.selected = isSelected;
                    oneRow.isAttributeSelected = isSelected;
                }
                return oneRow;
            });
            this.setState({
                rowsAttr: rows,
                allAttributesSelected: isSelected
            },
            () => {
                this.setState({
                    pagedRowsAttr: this.getItemsToShow(this.state.pageAttr,
                    this.state.perPageAttr, 'AttributeTable')
                }, () => {
                    this.updateValueTableRows();
                });
            });
        } else {
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            if (this.state.pagedRowsAttr[rowId].disableCheckbox === true) {
                return;
            } // End hack.

            // Process only the entries in the current page ( pagedRowsAttr )
            rows = [...this.state.pagedRowsAttr];
            rows[rowId].selected = isSelected;
            // Find the entry in the full array and set 'isAttributeSelected' accordingly
            // The property 'isAttributeSelected' is used to build the LDAP entry to add.
            // The row ID cannot be used since it changes with the pagination.
            const attrName = this.state.pagedRowsAttr[rowId].cells[0];
            const allItems = [...this.state.rowsAttr];
            const index = allItems.findIndex(item => item.cells[0] === attrName);
            allItems[index].isAttributeSelected = isSelected;
            const selectedAttributes = allItems
                .filter(item => item.isAttributeSelected)
                .map(attrObj => [attrObj.attributeName, attrObj.cells[1]]);
            this.setState({
                rowsAttr: allItems,
                pagedRowsAttr: rows,
                selectedAttributes
            }, () => {
                this.updateValueTableRows();
            });
        }
    };

    updateAttributeTableRows = () => {
        // TODO: To optimize!
        // Do not process objectClasses that are already selected.
        const ocToProcess = this.state.allObjectClassesSelected
            ? [...this.state.rowsOc]
            : [...this.state.selectedObjectClasses];

        const rowsAttr = [];
        const attrList = []; // Used to make sure we don't duplicate attributes present in several objectClasses.

        ocToProcess.map(oc => {
            // console.log(`oc.cells[0] = ${oc.cells[0]}`);
            // Rebuild the attribute arrays.
            const required = oc.cells[1].split(',');
            const optional = oc.cells[2].split(',');

            for (const attr of required) {
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
                                <React.Fragment>
                                    <strong>{attr}</strong>
                                </React.Fragment>
                            )
                        },
                        oc.cells[0]]
                    });
                }
            }

            for (const attr of optional) {
                if (attr === '') {
                    continue;
                }
                if (!attrList.includes(attr)) {
                    attrList.push(attr);
                    rowsAttr.push({
                        attributeName: attr,
                        cells: [attr, oc.cells[0]]
                    });
                }
            }
        });

        // Update the rows where user can select the attributes.
        this.setState({
            rowsAttr,
            selectedAttributes: rowsAttr.filter(item => item.isAttributeSelected)
                .map(attrObj => [attrObj.attributeName, attrObj.cells[1]]),
            itemCountAttr: rowsAttr.length,
            pagedRowsAttr: this.getItemsToShow(this.state.pageAttr, this.state.perPageAttr,
                'AttributeTable')
        });
    };

    resetNamingAttribute () {
        let rowsValues = [];
        let index = 0;

        this.state.rowsValues.map(row => {
            rowsValues.push({
                objectClass: row.objectClass,
                rowEditValidationRules: row.rowEditValidationRules,
                cells: [
                    {
                        title: (value, rowIndex, cellIndex, props) => (
                            <React.Fragment>
                                <EditableTextCell
                                    value={value}
                                    rowIndex={rowIndex}
                                    cellIndex={cellIndex}
                                    props={props}
                                    handleTextInputChange={this.handleTextInputChange}
                                    isDisabled
                                    inputAriaLabel={row.cells[0].props.value}
                                />
                                {this.state.namingAttrPropsName === row.cells[0].props.name &&
                                    <Popover
                                        headerContent={<div>Naming Attribute</div>}
                                        bodyContent={
                                            <div>
                                                The "<code>attribute: value</code>" pair on this row will be used
                                                to generate the RDN ( Relative Distinguished Name ) of the LDAP entry.
                                            </div>
                                        }
                                    >
                                        <a href="#">Naming Attribute</a>
                                    </Popover>
                                }
                            </React.Fragment>
                        ),
                        props: {
                            value: row.cells[0].props.value,
                            name: row.cells[0].props.name,
                        }
                    },
                    {
                        title: row.cells[1].title,
                        props: {
                            value: row.cells[1].props.value,
                            name: row.cells[1].props.name,
                        }
                    }
                ]
            });
            index++;
        });

        this.setState({
            rowsValues
        });
    }

    updateValueTableRows () {
        // TODO: To optimize!
        // Do not process attributes that are already selected.
        const attrsToProcess = this.state.allAttributesSelected
            ? this.state.rowsAttr.map(attrObj => [attrObj.attributeName, attrObj.cells[1]])
            // [...this.state.rowsAttr]
            : [...this.state.selectedAttributes];

        const rowsValues = [];
        let index = 0;

        let namingRowIndex = this.state.namingRowIndex;
        let namingAttribute = this.state.namingAttribute;
        let namingAttrPropsName = this.state.namingAttrPropsName;

        attrsToProcess.map(attrData => {
            const attrName = attrData[0];
            const attrPropsName = `uniqueId_${attrName}_${index}`;
            rowsValues.push({
                objectClass: attrData[1],
                rowEditValidationRules: this.attributeValidationRules,
                cells: [
                    {
                        title: (value, rowIndex, cellIndex, props) => (
                            <React.Fragment>
                                <EditableTextCell
                                    value={value}
                                    rowIndex={rowIndex}
                                    cellIndex={cellIndex}
                                    props={props}
                                    handleTextInputChange={this.handleTextInputChange}
                                    isDisabled
                                    inputAriaLabel={attrName}
                                />
                                {(namingAttrPropsName === attrPropsName || namingRowIndex === -1) &&
                                    <Popover
                                        headerContent={<div>Naming Attribute</div>}
                                        bodyContent={
                                            <div>
                                                The "<code>attribute: value</code>" pair on this row will be used
                                                to generate the RDN ( Relative Distinguished Name ) of the LDAP entry.
                                            </div>
                                        }
                                    >
                                        <a href="#">Naming Attribute</a>
                                    </Popover>
                                }
                            </React.Fragment>
                        ),
                        props: {
                            value: attrName,
                            name: attrPropsName // `uniqueId_${attrName}_${index}`
                        }
                    },
                    {
                        title: (value, rowIndex, cellIndex, props) => (
                            <EditableTextCell
                                value={value}
                                rowIndex={rowIndex}
                                cellIndex={cellIndex}
                                props={props}
                                handleTextInputChange={this.handleTextInputChange}
                                inputAriaLabel={'_' + value} // To avoid empty values.
                            />
                        ),
                        props: {
                            value: '',
                            name: `uniqueId_${attrName}_value_${index}`
                        }
                    }
                ]
            });

            // Set a default naming attribute to the first attribute if none is set
            if (namingRowIndex === -1) {
                namingRowIndex = 0;
                namingAttribute = attrsToProcess[0][0];
                namingAttrPropsName = attrPropsName;
            }
            index++;
        });

        // Update the rows where user can set the values.
        this.setState({
            rowsValues,
            namingRowIndex,
            namingAttribute,
            namingAttrPropsName
        });
    };

    handleTextInputChange = (newValue, evt, rowIndex, cellIndex) => {
        const newRows = Array.from(this.state.rowsValues);
        newRows[rowIndex].cells[cellIndex].props.editableValue = newValue;
        this.setState({
            rowsValues: newRows
        });
        const index = this.state.namingRowIndex;
        if (rowIndex === index) {
            this.setState({
                namingValue: newValue
            });
        }
    };

    updateEditableRows = (evt, type, isEditable, rowIndex, validationErrors) => {
        const newRows = Array.from(this.state.rowsValues);
        if (validationErrors && Object.keys(validationErrors).length) {
            newRows[rowIndex] = validateCellEdits(newRows[rowIndex], type, validationErrors);
            this.setState({ rowsValues: newRows });
            return;
        }

        if (type === 'cancel') {
            newRows[rowIndex] = cancelCellEdits(newRows[rowIndex]);
            this.setState({
                rowsValues: newRows,
                noEmptyValue: newRows.find(el => el.cells[1].props.value === '') === undefined
            });
            return;
        }

        newRows[rowIndex] = applyCellEdits(newRows[rowIndex], type);
        this.setState({
            rowsValues: newRows
        }, () => {
            // Check if there is any empty value.
            const found = newRows.find(el => el.cells[1].props.value === '');
            if (found === undefined) { // The return value is 'undefined' if there is no empty value.
                this.setState({ noEmptyValue: type === 'save' }); // Disable on edit ( type='edit' ).
            } else {
                this.setState({ noEmptyValue: false });
            }
        });
    };

    generateLdifData = () => {
        const objectClassData = ['top'];
        const attribute = this.state.namingAttribute;
        const value = this.state.namingValue;
        const valueData = [];
        for (const item of this.state.rowsValues) {
            const attrName = item.cells[0].props.value;
            const oc = item.objectClass;
            if (oc && !objectClassData.includes(oc)) {
                objectClassData.push(oc);
            }
            valueData.push(`${attrName}: ${item.cells[1].props.value}`);
        }

        const ocArray = objectClassData.map(oc => `ObjectClass: ${oc}`);
        const dnLine = `dn: ${attribute}=${value},${this.props.wizardEntryDn}`;
        const ldifArray = [
            dnLine,
            ...ocArray,
            ...valueData
        ]
        this.setState({ ldifArray });
    }

    actionResolver = (rowData, { rowIndex }) => {
        const myAttr = rowData.cells[0].props.value;
        const myVal = rowData.cells[1].props.value;
        const myName = rowData.cells[0].props.name;
        const namingAction = this.state.namingAttrPropsName === myName
            ? []
            : [{
                title: 'Set as Naming Attribute',
                onClick: (event, rowId, rowData, extra) => {
                    const rowsValues = [...this.state.rowsValues];
                    this.setState({
                        // rowsValues,
                        namingAttrPropsName: myName,
                        namingRowIndex: rowIndex,
                        namingAttribute: myAttr,
                        namingValue: myVal
                    }, () => {
                        this.resetNamingAttribute();
                    });
                }
            }];

        const duplicationAction =
            this.isAttributeSingleValued(rowData.cells[0].props.value)
                ? []
                : [{
                    title: 'Duplicate Attribute',
                    onClick: (event, rowId, rowData, extra) => {
                        const rowsValues = [...this.state.rowsValues];
                        const attrPropsName = `uniqueId_${myAttr}_${rowsValues.length}`;
                        const newItem = { // TODO. Code is duplicated!!
                            objectClass: rowData.objectClass,
                            rowEditValidationRules: this.attributeValidationRules,
                            cells:
                            [{
                                title: (value, rowIndex, cellIndex, props) => (
                                    <React.Fragment>
                                        <EditableTextCell
                                            value={value}
                                            rowIndex={rowIndex}
                                            cellIndex={cellIndex}
                                            props={props}
                                            handleTextInputChange={this.handleTextInputChange}
                                            isDisabled
                                            inputAriaLabel={myAttr}
                                        />
                                        {(this.state.namingAttrPropsName === attrPropsName) &&
                                            <Popover
                                                headerContent={<div>Naming Attribute</div>}
                                                bodyContent={
                                                    <div>
                                                        The "<code>attribute: value</code>" pair on this row will be used
                                                        to generate the RDN ( Relative Distinguished Name ) of the LDAP entry.
                                                    </div>
                                                }
                                            >
                                                <a href="#">Naming Attribute</a>
                                            </Popover>
                                        }
                                    </React.Fragment>
                                ),
                                props: {
                                    value: myAttr,
                                    name: attrPropsName
                                }
                            },
                            {
                                title: (value, rowIndex, cellIndex, props) => (
                                    <EditableTextCell
                                        value={value}
                                        rowIndex={rowIndex}
                                        cellIndex={cellIndex}
                                        props={props}
                                        handleTextInputChange={this.handleTextInputChange}
                                        inputAriaLabel={'_' + value} // To avoid empty values.
                                    />
                                ),
                                props: {
                                    value: '',
                                    name: `uniqueId_${myAttr}_value_${rowsValues.length}`
                                }
                            }
                        ]
                    };
                    // Insert the duplicate item right below the original.
                    rowsValues.splice(rowIndex + 1, 0, newItem);
                    // Update the values table.
                    this.setState({
                        rowsValues
                    });
                }
            }];

        const removalAction = this.state.rowsValues.length > 1 // No point to have an empty table.
            ? [
                {
                    title: 'Remove this Row',
                    onClick: (event, rowId, rowData, extra) => {
                        const myName = rowData.cells[0].props.name;
                        const rowsValues = this.state.rowsValues.filter((aRow) => aRow.cells[0].props.name !== myName);
                        this.setState({ rowsValues });
                    }
                }
            ]
            : [];

        const namLen = namingAction.length;
        const dupLen = duplicationAction.length;
        const remLen = removalAction.length;
        const firstSeparator = namLen * (dupLen + remLen) > 0 ? [{ isSeparator: true }] : [];
        const secondSeparator = dupLen * remLen > 0 ? [{ isSeparator: true }] : [];

        return [
            ...namingAction,
            ...firstSeparator,
            ...duplicationAction,
            ...secondSeparator,
            ...removalAction
        ];
    }

    handleSelectClick = newState => {
        this.setState({
            allObjectClassesSelected: newState === 'all'
        });

        if (newState === 'none') {
            const newRows = this.state.rowsOc.map(item => {
                item.isOcSelected = false;
                item.selected = false;
                return item;
            });
            this.setState({
                // selectedItems: []
                selectedObjectClasses: [],
                rowsOc: newRows
            }, () => { this.updateAttributeTableRows() });
        } else if (newState === 'page') {
            let newRows = [];
            const rows = this.state.pagedRowsOc.map(item => {
                const isSelected = item.selected;
                newRows = isSelected ? [...newRows] : [...newRows, item];
                item.isOcSelected = true;
                item.selected = true;
                return item;
            });

            this.setState((prevState, props) => {
                return {
                    selectedObjectClasses: prevState.selectedObjectClasses.concat(newRows),
                    pagedRowsOc: rows
                };
            }, () => { this.updateAttributeTableRows() });
        } else {
            const newRows = this.state.rowsOc.map(item => {
                item.isOcSelected = true;
                item.selected = true;
                return item;
            });
            this.setState({
                rowsOc: newRows
            }, () => { this.updateAttributeTableRows() });
        }
    };

    onDropDownToggle = isOpen => {
        this.setState({
            isDropDownOpen: isOpen
        });
    };

    onDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isDropDownOpen: !prevState.isDropDownOpen };
        });
    };

    buildSelectDropdown () {
        const { isDropDownOpen, selectedObjectClasses, allObjectClassesSelected } = this.state;
        const numSelected = allObjectClassesSelected === true
            ? this.state.rowsOc.length
            : this.state.rowsOc.filter(item => item.selected).length;
        const allSelected = numSelected === this.state.rowsOc.length;
        const anySelected = numSelected > 0;
        const someChecked = anySelected ? null : false;
        const isChecked = allSelected ? true : someChecked;
        const items = this.state.rowsOc.filter(item => item.selected).map((oc) =>
            <DropdownItem key={oc.cells[0]}>{oc.cells[0]}</DropdownItem>
        );

        return (
            <Dropdown
                className="ds-margin-top-lg"
                onSelect={this.onDropDownSelect}
                position={DropdownPosition.left}
                toggle={
                    <DropdownToggle
                        splitButtonItems={[
                            <DropdownToggleCheckbox
                                id="example-checkbox-2"
                                key="split-checkbox"
                                aria-label={anySelected ? 'Deselect all' : 'Select all'}
                                isChecked={allSelected ? true : someChecked}
                                isDisabled={!anySelected}
                                onClick={() => {
                                    anySelected ? this.handleSelectClick('none') : this.handleSelectClick('all');
                                }}
                            />
                        ]}
                        onToggle={this.onDropDownToggle}
                    >
                        {numSelected !== 0 ? <React.Fragment>{numSelected} selected </React.Fragment> : <>0 selected </>}
                    </DropdownToggle>
                }
                isOpen={isDropDownOpen}
                dropdownItems={items}
            />
        );
    }

    render () {
        const {
            loading, itemCountOc, pageOc, perPageOc, columnsOc, pagedRowsOc,
            itemCountAttr, pageAttr, perPageAttr, columnsAttr, pagedRowsAttr,
            commandOutput, namingAttribute, namingValue, stepIdReached,
            itemCount, pageAddUser, perPageAddUser, ldifArray, columnsValue,
            rowsValues, noEmptyValue,resultVariant
        } = this.state;

        const loadingStateRows = [{
            heightAuto: true,
            cells: [
                {
                    props: { colSpan: 8 },
                    title: (
                        <Bullseye key="add-entry-bulleye" >
                            <Title headingLevel="h2" size="lg" key="loading-title" >
                                Loading...
                            </Title>
                            <center><Spinner size="xl" key="loading-spinner" /></center>
                        </Bullseye>
                    )
                },
                'Loading...',
                'Loading...'
            ]
        }];

        const objectClassStep = (
            <React.Fragment>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Select ObjectClasses
                    </Text>
                </TextContent>
                { loading &&
                    <div>
                        <Bullseye className="ds-margin-top-xlg" key="add-entry-bulleye" >
                            <Title headingLevel="h3" size="lg" key="loading-title">
                                Loading ObjectClasses ...
                            </Title>
                        </Bullseye>
                        <Spinner className="ds-center" size="lg" key="loading-spinner" />
                    </div>
                }
                <div className={loading ? "ds-hidden" : ""}>
                    {this.buildSelectDropdown()}
                    <Pagination
                        value="ObjectClassTable"
                        itemCount={this.state.itemCountOc}
                        page={this.state.pageOc}
                        perPage={this.state.perPageOc}
                        onSetPage={this.onSetPageOc}
                        widgetId="pagination-step-objectclass"
                        onPerPageSelect={this.onPerPageSelectOc}
                        variant="top"
                        isCompact
                    />
                    <Table
                        cells={columnsOc}
                        rows={pagedRowsOc}
                        canSelectAll={false}
                        onSelect={this.onSelectOc}
                        variant={TableVariant.compact}
                        aria-label="Pagination All ObjectClasses"
                    >
                        <TableHeader />
                        <TableBody />
                    </Table>
                </div>
            </React.Fragment>
        );

        const attributeStep = (
            <React.Fragment>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Select Attributes
                    </Text>
                </TextContent>
                <Pagination
                    itemCount={itemCountAttr}
                    page={pageAttr}
                    perPage={perPageAttr}
                    onSetPage={this.onSetPageAttr}
                    widgetId="pagination-step-attributes"
                    onPerPageSelect={this.onPerPageSelectAttr}
                    isCompact
                />
                <Table
                    cells={columnsAttr}
                    rows={pagedRowsAttr}
                    onSelect={this.onSelectAttr}
                    variant={TableVariant.compact}
                    aria-label="Pagination Attributes"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </React.Fragment>
        );

        const myTitle = namingValue === '' ? 'Invalid Naming Attribute - Empty Value!' : 'DN ( Distinguished Name )';
        const entryValuesStep = (
            <React.Fragment>
                { (namingAttribute === '') &&
                    <div className="ds-margin-bottom-md">
                        <Alert
                            variant="warning"
                            isInline
                            title="Please select a row for the naming attribute."
                        />
                    </div>
                }
                <Alert
                    variant={namingValue === ''
                        ? 'warning'
                        : 'success'}
                    isInline
                    title={myTitle}
                >
                    <div className={namingAttribute === '' ? "ds-hidden" : ""}>
                        <b>Entry DN:&nbsp;&nbsp;&nbsp;</b>{(namingAttribute ? namingAttribute : "??????")}={namingValue ? namingValue : "??????"},{this.props.wizardEntryDn}
                    </div>
                </Alert>
                <Table
                    actionResolver={this.actionResolver}
                    onRowEdit={this.updateEditableRows}
                    aria-label="Editable Rows Table"
                    variant={TableVariant.compact}
                    cells={columnsValue}
                    rows={rowsValues}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </React.Fragment>
        );

        const ldifListItems = ldifArray.map(line =>
            <SimpleListItem key={line} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const entryCreationStep = (
            <div>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant="info"
                        isInline
                        title="LDIF Content for User Creation"
                    />
                </div>
                <Card isHoverable>
                    <CardBody>
                        <SimpleList aria-label="LDIF data User">
                            {ldifListItems}
                        </SimpleList>
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
                        title="Result for Entry Creation"
                    >
                        {commandOutput}
                    </Alert>
                </div>
                {resultVariant === 'danger' &&
                    <Card isHoverable>
                        <CardTitle>LDIF Data</CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>
                }
            </div>
        );

        const addEntrySteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 6,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Select ObjectClasses',
                component: objectClassStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 6,
                enableNext: this.state.selectedObjectClasses.length > 0 ||
                    this.state.allObjectClassesSelected
            },
            {
                id: 3,
                name: 'Select Attributes',
                component: attributeStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 6,
            },
            {
                id: 4,
                name: 'Set Values',
                component: entryValuesStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 6,
                // Empty value for namingValue is already checked by noEmptyValue variable.
                enableNext: noEmptyValue && (namingAttribute !== '')
            },
            {
                id: 5,
                name: 'Create LDAP Entry',
                component: entryCreationStep,
                nextButtonText: 'Create',
                canJumpTo: stepIdReached >= 5 && stepIdReached < 6,
            },
            {
                id: 6,
                name: 'Review Result',
                component: entryReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 6 && stepIdReached < 6,
                hideBackButton: true
            }
        ];

        return (
            // <GenericWizard
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                steps={addEntrySteps}
                title="Add a new LDAP Entry"
                description={`Parent DN: ${this.props.wizardEntryDn}`}
                onNext={this.onNext}
            />
        );
    }
}

export default AddLdapEntry;
