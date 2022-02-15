import React from 'react';
import {
    Alert,
    BadgeToggle,
    Card,
    CardHeader,
    CardBody,
    CardFooter,
    CardTitle,
    Dropdown, DropdownItem, DropdownPosition,
    Form,
    Grid,
    GridItem,
    Pagination,
    SimpleList,
    SimpleListItem,
    Text,
    TextContent,
    TextVariants,
    Wizard,
} from '@patternfly/react-core';
import {
    EditableTextCell,
    EditableSelectInputCell,
    Table, TableHeader, TableBody, TableVariant, headerCol,
    applyCellEdits,
    cancelCellEdits,
    getErrorTextByValidator,
    validateCellEdits,
} from '@patternfly/react-table';
import EditableTable from '../../lib/editableTable.jsx';
import {
    createLdapEntry,
    generateUniqueId
} from '../../lib/utils.jsx';

class AddUser extends React.Component {
    constructor (props) {
        super(props);

        this.inetorgPersonArray = [
            'audio', 'businessCategory', 'carLicense', 'departmentNumber', 'displayName', 'employeeNumber',
            'employeeType', 'givenName', 'homePhone', 'homePostalAddress', 'initials', 'jpegPhoto', 'labeledURI',
            'mail', 'manager', 'mobile', 'o', 'pager', 'photo', 'roomNumber', 'secretary', 'uid', 'userCertificate',
            'x500UniqueIdentifier', 'preferredLanguage', 'userSMIMECertificate', 'userPKCS12'
        ];
        this.organizationalPersonArray = [
            'title', 'x121Address', 'registeredAddress', 'destinationIndicator', 'preferredDeliveryMethod', 'telexNumber',
            'teletexTerminalIdentifier', 'internationalISDNNumber', 'facsimileTelephoneNumber',
            'street', 'postOfficeBox', 'postalCode', 'postalAddress', 'physicalDeliveryOfficeName', 'ou', 'st', 'l'
        ];
        this.personOptionalArray = [
            'userPassword', 'telephoneNumber', 'seeAlso', 'description'
        ];
        this.operationalOptionalArray = ['nsRoleDN'];
        this.singleValuedAttributes = [
            'preferredDeliveryMethod', 'displayName', 'employeeNumber', 'preferredLanguage'
        ];

        this.requiredAttributes = ['cn', 'sn'];

        this.attributeValidationRules = [
            {
                name: 'required',
                validator: value => value.trim() !== '',
                errorText: 'This field is required'
            }
        ];

        this.state = {
            alertVariant: 'info',
            namingRowID: -1,
            namingAttrVal: '',
            namingAttr: '',
            namingVal: '',
            ldifArray: [],
            cleanLdifArray: [],
            savedRows: [],
            commandOutput: '',
            resultVariant: 'default',
            allAttributesSelected: false,
            stepIdReached: 1,
            // currentStepId: 1,
            itemCountAddUser: 0,
            pageAddUser: 1,
            perPageAddUser: 10,
            columnsUser: [
                { title: 'Attribute Name', cellTransforms: [headerCol()] },
                { title: 'From ObjectClass' }
            ],
            rowsUser: [],
            pagedRowsUser: [],
            selectedAttributes: ['cn', 'sn', 'nsRoleDN'],
            isAttrDropDownOpen: false,
            // Values
            noEmptyValue: false,
            columnsValues: [
                'Attribute',
                'Value'
            ],
            // Review step
            reviewValue: '',
            reviewInvalidText: 'Invalid LDIF',
            reviewIsValid: true,
            reviewValidated: 'default',
            // reviewHelperText: 'LDIF data'
            editableTableData: [],
            rdn: "",
            rdnValue: "",
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            if (id === 3) {
                // Update the values table.
                this.updateValuesTableRows();
            } else if (id === 4) {
                // Generate the LDIF data.
                this.generateLdifData();
            } else if (id === 5) {
                // Create the LDAP entry.
                const myLdifArray = this.state.ldifArray;
                createLdapEntry(this.props.editorLdapServer,
                    myLdifArray,
                    (result) => {
                        this.setState({
                            commandOutput: result.output,
                            resultVariant: result.errorCode === 0 ? 'success' : 'danger'
                        }, () => {
                            this.props.onReload();
                        });
                        // Update the wizard operation information.
                        const myDn = myLdifArray[0].substring(4);
                        const opInfo = {
                            operationType: 'ADD',
                            resultCode: result.errorCode,
                            time: Date.now(),
                            entryDn: myDn,
                            relativeDn: this.state.namingAttrVal
                        }
                        this.props.setWizardOperationInfo(opInfo);
                    }
                );
            }
        };

        this.onBack = ({ id }) => {
            if (id === 3) {
                // true ==> Do not check the attribute selection when navigating back.
                this.updateValuesTableRows(true);
            }
        };

        // End constructor().
    }

    componentDidMount () {
        // console.log('In AddUser - componentDidMount()');
        // TODO:
        // Use an ldapsearch request on the schema entry.
        // Check with RHDS Engineering ( dsconf? to retrieve the list of attrs for a given oc)
        // FIXME:
        // disableCheckbox doesn't exist for version 2020.06!
        // Need to upgrade...
        const attributesArray = [];
        attributesArray.push({
            cells: ['cn', 'Person'],
            selected: true,
            isAttributeSelected: true,
            disableCheckbox: true
        });
        attributesArray.push({
            cells: ['sn', 'Person'],
            selected: true,
            isAttributeSelected: true,
            disableCheckbox: true
        });
        this.personOptionalArray.map(attr => {
            attributesArray.push({ cells: [attr, 'Person'] });
        });
        this.organizationalPersonArray.map(attr => {
            attributesArray.push({ cells: [attr, 'OrganizationalPerson'] });
        });
        this.inetorgPersonArray.map(attr => {
            attributesArray.push({ cells: [attr, 'InetOrgPerson'] });
        });
        this.operationalOptionalArray.map(attr => {
            attributesArray.push({ cells: [attr, ''] });
        });

        // Sort the attributes
        attributesArray.sort((a, b) => (a.cells[0] > b.cells[0]) ? 1 : -1);

        this.setState({
            itemCountAddUser: attributesArray.length,
            rowsUser: attributesArray,
            pagedRowsUser: attributesArray.slice(0, this.state.perPageAddUser)
        });
    }

    onSetPageAddUser = (_event, pageNumber) => {
        this.setState({
            pageAddUser: pageNumber,
            pagedRowsUser: this.getAttributesToShow(pageNumber, this.state.perPageAddUser)
        });
    };

    onPerPageSelectAddUser = (_event, perPage) => {
        this.setState({
            pageAddUser: 1,
            perPageAddUser: perPage,
            pagedRowsUser: this.getAttributesToShow(1, perPage)
        });
    };

    getAttributesToShow (page, perPage) {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        const newRows = this.state.rowsUser.slice(start, end);
        return newRows;
    }

    isAttributeSingleValued = attr => {
        return this.singleValuedAttributes.includes(attr);
    };

    isAttributeRequired = attr => {
        return this.requiredAttributes.includes(attr);
    }

    onSelect = (event, isSelected, rowId) => {
        let rows;
        if (rowId === -1) {
            // Process the full table entries ( rowsUser )
            rows = this.state.rowsUser.map(oneRow => {
                oneRow.selected = isSelected;
                oneRow.isAttributeSelected = isSelected;
                return oneRow;
            });
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            // Both 'cn' and 'sn' ( first 2 elements in the table ) are mandatory.
            // TODO: https://www.patternfly.org/v4/components/table#selectable
            // ==> disableSelection: true
            rows[0].selected = true;
            rows[1].selected = true;
            rows[0].isAttributeSelected = true;
            rows[1].isAttributeSelected = true;
            const selectedAttributes = ['cn', 'sn'];
            this.setState({
                rowsUser: rows,
                allAttributesSelected: isSelected,
                selectedAttributes
            },
            () => {
                this.setState({
                    pagedRowsUser: this.getAttributesToShow(this.state.pageAddUser, this.state.perPageAddUser)
                });
                this.updateValuesTableRows();
            });
        } else {
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            if (this.state.pagedRowsUser[rowId].disableCheckbox === true) {
                return;
            } // End hack.

            // Process only the entries in the current page ( pagedRowsUser )
            rows = [...this.state.pagedRowsUser];
            rows[rowId].selected = isSelected;
            // Find the entry in the full array and set 'isAttributeSelected' accordingly
            // The property 'isAttributeSelected' is used to build the LDAP entry to add.
            // The row ID cannot be used since it changes with the pagination.
            const attrName = this.state.pagedRowsUser[rowId].cells[0];
            const allItems = [...this.state.rowsUser];
            const index = allItems.findIndex(item => item.cells[0] === attrName);
            allItems[index].isAttributeSelected = isSelected;
            const selectedAttributes = allItems
                .filter(item => item.isAttributeSelected)
                .map(selectedAttr => selectedAttr.cells[0]);

            this.setState({
                rowsUser: allItems,
                pagedRowsUser: rows,
                selectedAttributes
            },
            () => this.updateValuesTableRows());
        }
    };

    updateValuesTableRows = (skipAttributeSelection) => {
        const newSelectedAttrs = this.state.allAttributesSelected
            ? ['cn', 'sn',
            ...this.personOptionalArray,
            ...this.organizationalPersonArray,
            ...this.inetorgPersonArray,
            ...this.operationalOptionalArray]
            : [...this.state.selectedAttributes];
        let namingRowID = this.state.namingRowID;
        let namingAttrVal = this.state.namingAttrVal
        let editableTableData = [];
        let namingAttr = this.state.namingAttr;
        let namingVal = this.state.namingVal;

        if (this.state.savedRows.length === 0) {
            editableTableData = newSelectedAttrs.map(attrName => {
                const obj = {
                    id: generateUniqueId(),
                    attr: attrName,
                    val: '',
                    required: false,
                    namingAttr: false,
                }
                return obj;
            });
            editableTableData.sort((a, b) => (a.attr > b.attr) ? 1 : -1);
            namingRowID = editableTableData[0].id,
            namingAttrVal = editableTableData[0].attr + "=" + editableTableData[0].val;
            namingAttr = editableTableData[0].attr;
            namingVal = editableTableData[0].val;
        } else {
            if (skipAttributeSelection) { // Do not check the attribute selection ( because it has not changed ).
                editableTableData = [...this.state.savedRows];
            } else {
                const arrayOfAttrObjects = [...this.state.savedRows];
                for (const myAttr of newSelectedAttrs) {
                    const found = arrayOfAttrObjects.find(el => el.attr === myAttr);
                    if (found === undefined) {
                        // The new attribute was not in the list of saved attributes, add it.
                        arrayOfAttrObjects.push({
                            id: generateUniqueId(),
                            attr: myAttr,
                            val: '',
                            required: false,
                            namingAttr: false,
                        });
                    }
                }
                // Remove the newly unselected attribute(s).
                editableTableData = arrayOfAttrObjects
                    .filter(datum => {
                        const attrName = datum.attr;
                        const found = newSelectedAttrs.find(attr => attr === attrName);
                        return (found !== undefined);
                    });

                // Sort the rows
                editableTableData.sort((a, b) => (a.attr > b.attr) ? 1 : -1);
                if (this.state.namingRowID === -1) {
                    namingRowID = editableTableData[0].id
                }
            }
        }

        // Update the attributes to process.
        this.setState({
            editableTableData,
            rdn: editableTableData[0].attr,
            namingRowID,
            namingAttrVal,
            namingAttr,
            namingVal,
        });
    };

    enableNextStep = (noEmptyValue) => {
        this.setState({ noEmptyValue });
    };

    setNamingRowID = (namingRowID) => {
        let namingAttrVal = "";
        let namingAttr = "";
        let namingVal = "";
        let rows = this.state.savedRows;

        if (rows.length === 0) {
            rows = this.state.editableTableData
        }
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

    onAttrDropDownToggle = isOpen => {
        this.setState({
            isAttrDropDownOpen: isOpen
        });
    };

    onAttrDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isAttrDropDownOpen: !prevState.isAttrDropDownOpen };
        });
    };

    buildAttrDropdown = () => {
        const { isAttrDropDownOpen, selectedAttributes } = this.state;
        const numSelected = selectedAttributes.length;
        const items = selectedAttributes.map((attr) =>
            <DropdownItem key={attr}>{attr}</DropdownItem>
        );

        return (
            <Dropdown
                className="ds-dropdown-padding"
                onSelect={this.onAttrDropDownSelect}
                position={DropdownPosition.left}
                toggle={
                    <BadgeToggle id="toggle-attr-select" onToggle={this.onAttrDropDownToggle}>
                        {numSelected !== 0 ? <>{numSelected} selected </> : <>0 selected </>}
                    </BadgeToggle>
                }
                isOpen={isAttrDropDownOpen}
                dropdownItems={items}
            />
        );
    }

    saveCurrentRows = (savedRows, namingID) => {
        this.setState({ savedRows },
            () => {
                // Update the naming information after the new rows have been saved.
                if (namingID != -1) { // The namingIndex is set to -1 if the row is not the naming one.
                    this.setNamingRowID(namingID);
                }
            });
    }

    generateLdifData = () => {
        const objectClassData = ['ObjectClass: top', 'ObjectClass: Person']; // ObjectClass 'Person' is required.
        if (this.state.allAttributesSelected) {
            objectClassData.push('ObjectClass: OrganizationalPerson',
                'ObjectClass: InetOrgPerson');
        }

        const valueData = [];
        for (const item of this.state.savedRows) {
            const attrName = item.attr;
            valueData.push(`${attrName}: ${item.val}`);
            if (objectClassData.length === 4) { // There will a maximum of 4 ObjectClasses.
                continue;
            }
            // TODO: Find a better logic!
            if ((!objectClassData.includes('ObjectClass: InetOrgPerson')) &&
            this.inetorgPersonArray.includes(attrName)) {
                objectClassData.push('ObjectClass: InetOrgPerson');
            }
            if (!objectClassData.includes('ObjectClass: OrganizationalPerson') &&
            this.organizationalPersonArray.includes(attrName)) {
                objectClassData.push('ObjectClass: OrganizationalPerson');
            }
        }

        const ldifArray = [
            `dn: ${this.state.namingAttrVal},${this.props.wizardEntryDn}`,
            ...objectClassData,
            ...valueData
        ];

        let cleanLdifArray = [...ldifArray];
        for (let idx in cleanLdifArray) {
            if (cleanLdifArray[idx].toLowerCase().startsWith("userpassword")) {
                cleanLdifArray[idx] = "userpassword: ********";
                break;
            }
        }

        this.setState({ ldifArray, cleanLdifArray });
    }

    render () {
        const {
            commandOutput,
            itemCountAddUser, pageAddUser, perPageAddUser, columnsUser, pagedRowsUser,
            ldifArray, cleanLdifArray, columnsValues, noEmptyValue, alertVariant,
            namingAttrVal, namingAttr, namingVal, resultVariant, editableTableData,
            stepIdReached
        } = this.state;

        const rdnValue = namingVal;
        const myTitle = (namingAttrVal === '' || rdnValue === '')
            ? 'Invalid Naming Attribute - Empty Value!'
            : 'DN ( Distinguished Name )';

        const userAttributesStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Select Entry Attributes
                        </Text>
                    </TextContent>
                    {this.buildAttrDropdown()}
                </div>
                <Pagination
                    itemCount={itemCountAddUser}
                    page={pageAddUser}
                    perPage={perPageAddUser}
                    onSetPage={this.onSetPageAddUser}
                    widgetId="pagination-options-menu-add-user"
                    onPerPageSelect={this.onPerPageSelectAddUser}
                    isCompact
                />
                <Table
                    cells={columnsUser}
                    rows={pagedRowsUser}
                    onSelect={this.onSelect}
                    variant={TableVariant.compact}
                    aria-label="Pagination User Attributes"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </>
        );

        const userValuesStep = (
            <React.Fragment>
                <Form autoComplete="off">
                    <Grid>
                        <GridItem className="ds-margin-top" span={12}>
                            <Alert
                                variant={namingAttr === '' || namingVal === ''
                                    ? 'warning'
                                    : 'success'}
                                isInline
                                title={myTitle}
                            >
                                <b>Entry DN:&nbsp;&nbsp;&nbsp;</b>{(namingAttr ? namingAttr : "??????")}={namingVal ? namingVal : "??????"},{this.props.wizardEntryDn}
                            </Alert>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Set Attribute Values
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                </Form>
                <GridItem className="ds-left-margin" span={11}>
                    <EditableTable
                        editableTableData={editableTableData}
                        isAttributeSingleValued={this.isAttributeSingleValued}
                        isAttributeRequired={this.isAttributeRequired}
                        enableNextStep={this.enableNextStep}
                        setNamingRowID={this.setNamingRowID}
                        saveCurrentRows={this.saveCurrentRows}
                        namingRowID={this.state.namingRowID}
                        namingAttr={this.state.namingAttr}
                    />
                </GridItem>
            </React.Fragment>
        );

        const ldifListItems = cleanLdifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const userCreationStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title="LDIF Content for User Creation"
                />
                <Card isHoverable>
                    <CardBody>
                        { (ldifListItems.length > 0) &&
                            <SimpleList aria-label="LDIF data User">
                                {ldifListItems}
                            </SimpleList>
                        }
                    </CardBody>
                </Card>
            </div>
        );

        let nb = -1;
        const ldifLines = ldifArray.map(line => {
            nb++;
            return { data: line, id: nb };
        })
        const userReviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title="Result for User Creation"
                >
                    {commandOutput}
                </Alert>
                {resultVariant === 'danger' &&
                    <Card isHoverable>
                        <CardTitle>
                            LDIF Data
                        </CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>
                }
            </div>
        );

        const addUserSteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 5,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Select Attributes',
                component: userAttributesStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 5
            },
            {
                id: 3,
                name: 'Set Values',
                component: userValuesStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 5,
                enableNext: noEmptyValue
            },
            {
                id: 4,
                name: 'Create User',
                component: userCreationStep,
                nextButtonText: 'Create User',
                canJumpTo: stepIdReached >= 4 && stepIdReached < 5
            },
            {
                id: 5,
                name: 'Review Result',
                component: userReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 5,
                hideBackButton: true
            }
        ];

        const title = <>
            Parent DN: &nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
        </>;

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                onNext={this.onNext}
                onBack={this.onBack}
                title="Add A User"
                description={title}
                steps={addUserSteps}
            />
        );
    }
}

export default AddUser;
