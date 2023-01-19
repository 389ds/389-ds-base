import React from 'react';
import {
    Alert,
    Card,
    CardHeader,
    CardBody,
    CardFooter,
    CardTitle,
    Pagination,
    Popover,
    SimpleList,
    SimpleListItem,
    Spinner,
    Text,
    TextArea,
    TextContent,
    TextVariants,
    Wizard,
} from '@patternfly/react-core';
import {
    Table, TableHeader, TableBody, TableVariant, headerCol
} from '@patternfly/react-table';
import EditableTable from '../../lib/editableTable.jsx';
import {
    createLdapEntry, generateUniqueId
} from '../../lib/utils.jsx';
import {
    // User.
    INET_ORG_ATTRS, ORG_PERSON_ATTRS,
    PERSON_REQ_ATTRS, PERSON_OPT_ATTRS,
    SINGLE_VALUED_ATTRS,
    // Organizational Unit.
    OU_REQ_ATTRS, OU_OPT_ATTRS,
    // Entry type.
    ENTRY_TYPE
} from '../../lib/constants.jsx';

class GenericUpdate extends React.Component {
    constructor (props) {
        super(props);

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
            ldifArray: [],
            savedRows: [],
            commandOutput: '',
            resultVariant: 'default',
            allAttributesSelected: false,
            stepIdReached: 1,
            itemCount: 0,
            page: 1,
            perPage: 10,
            columnsAttrs: [
                { title: 'Attribute Name', cellTransforms: [headerCol()] },
                { title: 'From ObjectClass' }
            ],
            rowsAttrs: [],
            pagedRowsAttrs: [],
            selectedAttributes: [],
            // Values
            noEmptyValue: false,
            columnsValues: [
                'Attribute',
                'Value'
            ],
            rowsValues: [],
            // Review step
            reviewValue: '',
            reviewInvalidText: 'Invalid LDIF',
            reviewIsValid: true,
            reviewValidated: 'default',
            editableTableData: [],
            namingAttr: '',
            adding: true,
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
                            commandOutput: result.errorCode === 0 ? 'Successfully added entry!' : 'Failed to add entry, error: ' + result.errorCode ,
                            resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                            adding: false,
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
        console.log('In GenericUpdate - componentDidMount()');
        // TODO:
        // Use an ldapsearch request on the schema entry.
        // Check with RHDS Engineering ( dsconf? to retrieve the list of attrs for a given oc)
        // FIXME:
        // disableCheckbox doesn't exist for version 2020.06!
        // Need to upgrade...
        let attributesArray = [];
        let namingAttr = '';
        switch (this.props.entryType) {
            case ENTRY_TYPE.user:
                this.requiredAttributes = ['cn', 'sn'];
                attributesArray.push({
                    cells: ['cn', 'Person'],
                    selected: true,
                    isAttributeSelected: true,
                    // disableCheckbox: true
                    disableSelection: true
                });
                attributesArray.push({
                    cells: ['sn', 'Person'],
                    selected: true,
                    isAttributeSelected: true,
                    // disableCheckbox: true
                    disableSelection: true
                });
                PERSON_OPT_ATTRS.map(attr => {
                    attributesArray.push({ cells: [attr, 'Person'] });
                });

                ORG_PERSON_ATTRS.map(attr => {
                    attributesArray.push({ cells: [attr, 'OrganizationalPerson'] });
                });
                INET_ORG_ATTRS.map(attr => {
                    attributesArray.push({ cells: [attr, 'InetOrgPerson'] });
                });
                break;
            case ENTRY_TYPE.ou:
                this.requiredAttributes = ['ou'];

                attributesArray.push({
                    cells: ['ou', 'OrganizationalUnit'],
                    selected: true,
                    isAttributeSelected: true,
                    // disableCheckbox: true
                    disableSelection: true,
                    namingAttrVal: "",
                });

                OU_OPT_ATTRS.map(attr => {
                    attributesArray.push({ cells: [attr, 'OrganizationalUnit'] });
                });
                namingAttr = 'ou';
                break;
            case ENTRY_TYPE.other:
                break;
            default:
                console.log(`Unknown type of LDAP entry (${this.props.entryType})`);
        }

        this.setState({
            selectedAttributes: [...this.requiredAttributes],
            itemCount: attributesArray.length,
            rowsAttrs: attributesArray,
            pagedRowsAttrs: attributesArray.slice(0, this.state.perPage),
            namingAttr: namingAttr,
        });
    }

    onSetPage = (_event, pageNumber) => {
        this.setState({
            page: pageNumber,
            pagedRowsAttrs: this.getAttributesToShow(pageNumber, this.state.perPage)
        });
    };

    onPerPageSelect = (_event, perPage) => {
        this.setState({
            page: 1,
            perPage: perPage,
            pagedRowsAttrs: this.getAttributesToShow(1, perPage)
        });
    };

    getAttributesToShow (page, perPage) {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        const newRows = this.state.rowsAttrs.slice(start, end);
        return newRows;
    }

    isAttributeSingleValued = attr => {
        return SINGLE_VALUED_ATTRS.includes(attr);
    };

    isAttributeRequired = attr => {
        switch (this.props.entryType) {
            case ENTRY_TYPE.user:
                return PERSON_REQ_ATTRS.includes(attr);
            case ENTRY_TYPE.ou:
                return OU_REQ_ATTRS.includes(attr);
            case ENTRY_TYPE.other:
                return PERSON_REQ_ATTRS.includes(attr);
            default:
                console.log(`Unknown type of LDAP entry (${this.props.entryType})`);
        }
    }

    onSelect = (event, isSelected, rowId) => {
        let rows;
        if (rowId === -1) {
            // Process the full table entries ( rowsAttrs )
            rows = this.state.rowsAttrs.map(oneRow => {
                // Change only the state of rows which can be selected.
                if (oneRow.disableSelection === false) {
                    oneRow.selected = isSelected;
                    oneRow.isAttributeSelected = isSelected;
                }
                return oneRow;
            });
            /* TEKO - May 27th 2021 - BEGIN
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            // Both 'cn' and 'sn' ( first 2 elements in the table ) are mandatory.
            // TODO: https://www.patternfly.org/v4/components/table#selectable
            // ==> disableSelection: true
            rows[0].selected = true;
            rows[1].selected = true;
            rows[0].isAttributeSelected = true;
            rows[1].isAttributeSelected = true;
            const selectedAttributes = ['cn', 'sn']; */
            this.setState({
                rowsAttrs: rows,
                allAttributesSelected: isSelected
                // TEKO - May 2021 // selectedAttributes
            },
            () => {
                this.setState({
                    pagedRowsAttrs: this.getAttributesToShow(this.state.page, this.state.perPage)
                });
                this.updateValuesTableRows();
            });
        } else {
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            // if (this.state.pagedRowsAttrs[rowId].disableCheckbox === true) {
            if (this.state.pagedRowsAttrs[rowId].disableSelection === true) {
                return;
            } // End hack.

            // Process only the entries in the current page ( pagedRowsAttrs )
            rows = [...this.state.pagedRowsAttrs];
            rows[rowId].selected = isSelected;
            // Find the entry in the full array and set 'isAttributeSelected' accordingly
            // The property 'isAttributeSelected' is used to build the LDAP entry to add.
            // The row ID cannot be used since it changes with the pagination.
            const attrName = this.state.pagedRowsAttrs[rowId].cells[0];
            let allItems = [...this.state.rowsAttrs];
            const index = allItems.findIndex(item => item.cells[0] === attrName);
            allItems[index].isAttributeSelected = isSelected;
            const selectedAttributes = allItems
                .filter(item => item.isAttributeSelected)
                .map(selectedAttr => selectedAttr.cells[0]);
            this.setState({
                rowsAttrs: allItems,
                pagedRowsAttrs: rows,
                selectedAttributes
            },
            () => this.updateValuesTableRows());
        }
    };

    updateValuesTableRows = (skipAttributeSelection) => {
        const newSelectedAttrs = this.state.allAttributesSelected
            ? ['cn', 'sn',
            ...PERSON_OPT_ATTRS,
            ...ORG_PERSON_ATTRS,
            ...INET_ORG_ATTRS]
            : [...this.state.selectedAttributes];

        let editableTableData = [];
        let namingRowID = this.state.namingRowID;
        let namingAttrVal = this.state.namingAttrVal;

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
            if (this.props.entryType == ENTRY_TYPE.ou) {
                // Organizational Unit
                for (const attr of editableTableData) {
                    if (attr.attr === "ou") {
                        // OU naming attribute
                        namingRowID = attr.id,
                        namingAttrVal = editableTableData[0].attr + "=" + editableTableData[0].val;

                        break;
                    }
                }
            } else {
                // Other
                namingRowID = editableTableData[0].id,
                namingAttrVal = editableTableData[0].attr + "=" + editableTableData[0].val;
            }
        } else {
            if (skipAttributeSelection) { // Do not check the attribute selection ( because it has not changed ).
                editableTableData = [...this.state.savedRows];
            } else {
                let arrayOfAttrObjects = [...this.state.savedRows];
                for (const myAttr of newSelectedAttrs) {
                    const found = arrayOfAttrObjects.find(el => el.attr === myAttr);
                    if (found === undefined) {
                        // The new attribute was not in the list of saved attributes.
                        // Add it.
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

                if (this.props.entryType == ENTRY_TYPE.ou && namingRowID === -1) {
                    // Organizational Unit
                    for (const attr of editableTableData) {
                        if (attr.attr === "ou") {
                            // OU naming attribute
                            namingRowID = attr.id,
                            namingAttrVal = editableTableData[0].attr + "=" + editableTableData[0].val;
                            break;
                        }
                    }
                }
            }
        }

        // Update the attributes to process.
        this.setState({
            editableTableData,
            rdn: editableTableData[0].attr,
            namingRowID,
            namingAttrVal,
        });
    };

    enableNextStep = (noEmptyValue) => {
        this.setState({ noEmptyValue });
    };

    setNamingRowID = (namingRowID) => {
        let namingAttrVal = "";
        for (const row of this.state.savedRows) {
            if (row.id === namingRowID) {
                namingAttrVal = row.attr + "=" + row.val;
                break;
            }
        }
        this.setState({
            namingRowID,
            namingAttrVal: namingAttrVal,
        });
    };

    saveCurrentRows = (savedRows, namingID) => {
        this.setState({ savedRows },
            () => {
                // Update the naming information after the new rows have been saved.
                if (namingID != -1) { // The namingID is set to -1 if the row is not the naming one.
                    this.setNamingRowID(namingID);
                }
            });
    };

    generateLdifData = () => {
        switch (this.props.entryType) {
            case ENTRY_TYPE.user:
                this.generateLdifDataUser();
                break;
            case ENTRY_TYPE.ou:
                this.generateLdifDataOu();
                break;
            case ENTRY_TYPE.other:
                this.generateLdifDataOther();
                break;
            default:
                console.log(`Unknown type of LDAP entry (${this.props.entryType})`);
        }
    }

    generateLdifDataUser = () => {
        // ObjectClass 'Person' is required.
        let objectClassData = ['ObjectClass: top', 'ObjectClass: Person'];
        if (this.state.allAttributesSelected) {
            objectClassData.push('ObjectClass: OrganizationalPerson',
                                 'ObjectClass: InetOrgPerson');
        }

        let valueData = [];
        for (const item of this.state.savedRows) {
            const attrName = item.attr;
            valueData.push(`${attrName}: ${item.val}`);
            if (objectClassData.length === 4) { // There will a maximum of 4 ObjectClasses.
                continue;
            }
            // TODO: Find a better logic!
            if ((!objectClassData.includes('ObjectClass: InetOrgPerson')) &&
            INET_ORG_ATTRS.includes(attrName)) {
                objectClassData.push('ObjectClass: InetOrgPerson');
            }
            if (!objectClassData.includes('ObjectClass: OrganizationalPerson') &&
            ORG_PERSON_ATTRS.includes(attrName)) {
                objectClassData.push('ObjectClass: OrganizationalPerson');
            }
        }

        const ldifArray = [
            `dn: ${this.state.namingAttrVal},${this.props.wizardEntryDn}`,
            ...objectClassData,
            ...valueData
        ];
        this.setState({ ldifArray });
    }

    generateLdifDataOu = () => {
        const objectClassData = ['ObjectClass: top', 'ObjectClass: organizationalUnit'];
        let valueData = [];
        let rdnValue = "";
        for (const item of this.state.savedRows) {
            const attrName = item.attr;
            valueData.push(`${attrName}: ${item.val}`);
            if (attrName == "ou") {
                rdnValue = item.val;
            }
        }

        const ldifArray = [
            `dn: ou=${rdnValue},${this.props.wizardEntryDn}`,
            ...objectClassData,
            ...valueData
        ]
        this.setState({ ldifArray });
    }

    render () {
        const {
            commandOutput, itemCount, page, perPage, columnsAttrs, pagedRowsAttrs,
            ldifArray, noEmptyValue, alertVariant, namingAttrVal, stepIdReached,
            resultVariant
        } = this.state;

        const attributesStep = (
            <div>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Select Entry Attributes
                    </Text>
                </TextContent>
                <Pagination
                    itemCount={itemCount}
                    page={page}
                    perPage={perPage}
                    onSetPage={this.onSetPage}
                    widgetId="pagination-options-menu-add-entry"
                    onPerPageSelect={this.onPerPageSelect}
                    isCompact
                />
                <Table
                    cells={columnsAttrs}
                    rows={pagedRowsAttrs}
                    onSelect={this.onSelect}
                    variant={TableVariant.compact}
                    aria-label="Pagination Attributes"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </div>
        );

        // const myTitle = (namingAttrVal === '' || namingAttrVal.split('=')[1] === '')
        const rdnValue = namingAttrVal.split('=')[1];
        const myTitle = (namingAttrVal === '' || rdnValue === '')
            ? 'Invalid RDN - Empty Value!'
            : 'DN ( Distinguished Name )';
        const missingRdn = (rdnValue === '' || rdnValue === undefined);

        const valuesStep = (
            <React.Fragment>
                <Alert
                    variant={alertVariant}
                    isInline
                    title={myTitle}
                >
                    {!missingRdn &&
                        <>
                            The full DN of the entry will be: <strong>{namingAttrVal},{this.props.wizardEntryDn}</strong>
                        </>
                    }
                </Alert>

                <EditableTable
                    editableTableData={this.state.editableTableData}
                    isAttributeSingleValued={this.isAttributeSingleValued}
                    isAttributeRequired={this.isAttributeRequired}
                    enableNextStep={this.enableNextStep}
                    setNamingRowID={this.setNamingRowID}
                    namingRowID={this.state.namingRowID}
                    saveCurrentRows={this.saveCurrentRows}
                    namingAttr={this.state.namingAttr}
                />
            </React.Fragment>
        );

        const ldifListItems = ldifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const creationStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title="LDIF Content for Entry Creation"
                />
                <Card isSelectable>
                    <CardBody>
                        { (ldifListItems.length > 0) &&
                            <SimpleList aria-label="LDIF data">
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

        const reviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title="Result for Entry Creation"
                >
                    {commandOutput}
                    {this.state.adding &&
                        <div>
                            <Spinner className="ds-left-margin" size="md" />
                            &nbsp;&nbsp;Adding entry ...
                        </div>
                    }
                </Alert>
                {resultVariant === 'danger' &&
                    <Card isSelectable>
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

        const creationStepName = this.props.entryType === ENTRY_TYPE.user
            ? 'Create User'
            : this.props.entryType === ENTRY_TYPE.ou
                ? 'Create Organizational Unit'
                : 'Create LDAP Entry';

        const addSteps = [
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
                component: attributesStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 5,
            },
            {
                id: 3,
                name: 'Set Values',
                component: valuesStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 5,
                enableNext: noEmptyValue
            },
            {
                id: 4,
                name: creationStepName,
                component: creationStep,
                nextButtonText: 'Create',
                canJumpTo: stepIdReached >= 4 && stepIdReached < 5,
            },
            {
                id: 5,
                name: 'Review Result',
                component: reviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 5 && stepIdReached < 5,
                hideBackButton: true,
                enableNext: !this.state.adding
            }
        ];

        // TODO - Update this for groups, roles, cos, etc
        const wizardTitle = this.props.entryType === ENTRY_TYPE.user
            ? 'Add A New User'
            : this.props.entryType === ENTRY_TYPE.ou
                ? 'Add An Organizational Unit'
                : 'Add A New LDAP Entry';

        const desc = <>
            Parent DN: &nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
        </>;

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                onNext={this.onNext}
                onBack={this.onBack}
                title={wizardTitle}
                description={desc}
                steps={addSteps}
            />
        );
    }
}

export default GenericUpdate;
