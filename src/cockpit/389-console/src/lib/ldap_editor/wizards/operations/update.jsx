import React from 'react';
import {
    Alert,
    Card,
    CardBody,
    CardTitle,
    Label,
    LabelGroup,
    SimpleList,
    SimpleListItem,
    Wizard,
} from '@patternfly/react-core';
import {
    Table,
    TableBody,
    TableHeader,
    breakWord
} from '@patternfly/react-table';
import EditableTable from '../../lib/editableTable.jsx';
import {
    b64DecodeUnicode,
    getBaseLevelEntryAttributes,
    getRdnInfo,
    getSingleValuedAttributes,
    generateUniqueId,
    modifyLdapEntry,
    foldLine
} from '../../lib/utils.jsx';
import {
    LDAP_OPERATIONS,
    BINARY_ATTRIBUTES,
    LDIF_MAX_CHAR_PER_LINE
} from '../../lib/constants.jsx';

class UpdateEntry extends React.Component {
    constructor (props) {
        super(props);

        /* this.binaryAttributes = ['jpegphoto', 'usercertificate', 'usercertificate;binary',
            'cacertificate', 'cacertificate;binary', 'nssymmetrickey']; */

        this.operationColumns = [
            { title: 'Statement' },
            { title: 'Attribute' },
            { title: 'Value', cellTransforms: [breakWord] }
        ];
        this.originalEntryRows = [];
        this.singleValuedAttributes = [];
        this.requiredAttributes = ['dn'];

        this.state = {
            stepIdReached: 1,
            editableTableData: [],
            savedRows: [],
            objectclasses: [],
            ldifArray: [],
            commandOutput: "",
            resultVariant: 'default',
            validMods: false,
            numOfChanges: 0,
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });

            if (id === 2) {
                this.updateValuesTableRows();
            } else if (id === 3) {
                // console.log(this.state.savedRows);
                this.generateUpdateData();
            } else if (id === 5) {
                const params = { serverId: this.props.editorLdapServer };
                modifyLdapEntry(params, this.state.ldifArray, (result) => {
                    if (result.errorCode === 0) {
                        result.output = "Successfully modified entry"
                    }
                    this.setState({
                        commandOutput: result.output,
                        resultVariant: result.errorCode === 0 ? 'success' : 'danger'
                    });
                    // Update the wizard operation information.
                    // const myDn = myLdifArray[0].substring(4);
                    const opInfo = {
                        operationType: 'MODIFY',
                        resultCode: result.errorCode,
                        time: Date.now()
                        // entryDn: myDn,
                        // relativeDn: this.state.namingAttrVal
                    }
                    this.props.setWizardOperationInfo(opInfo);
                });
            }
        };

        this.onBack = ({ id }) => {
            if (id === 2) {
                this.updateValuesTableRows();
            }
        };

        // End constructor().
    }

    componentDidMount () {
        // const entryRows = [];
        getSingleValuedAttributes(this.props.editorLdapServer, // TODO: Remove the getSingleValuedAttributes() part once the EditableTable class is ready!
            (myAttrs) => {
                this.singleValuedAttributes = [...myAttrs];
            }
        );

        getBaseLevelEntryAttributes(this.props.editorLdapServer,
            this.props.wizardEntryDn,
            (entryDetails) => {
                let objectclasses = [];
                const rdnInfo = getRdnInfo(this.props.wizardEntryDn);
                entryDetails
                .filter(data => (data.attribute + data.value !== '' && // Filter out empty lines
                data.attribute !== '???: ')) // and data for empty suffix(es) and in case of failure.
                .map(line => {
                    const obj = {};
                    const attr = line.attribute;
                    const attrLowerCase = attr.trim().toLowerCase();
                    let namingAttribute = false;
                    let val = line.value.substring(1).trim();

                    if (attrLowerCase === "objectclass") {
                        objectclasses.push(val);
                    } else {
                        // Base64 encoded values
                        if (line.value.substring(0, 2) === '::') {
                            val = line.value.substring(3);
                            if (BINARY_ATTRIBUTES.includes(attrLowerCase)) {
                                // obj.fileUpload = true;
                                // obj.isDisabled = true;
                                if (attrLowerCase === 'jpegphoto') {
                                    const myPhoto = (<img
                                        src={`data:image/png;base64,${val}`}
                                        alt=""
                                        style={{ width: '48px' }} // height will adjust automatically.
                                        />);
                                    val = myPhoto;
                                } else if (attrLowerCase === 'nssymmetrickey') {
                                    // TODO: Check why the decoding of 'nssymmetrickey is failing...
                                    //   https://access.redhat.com/documentation/en-us/red_hat_directory_server/10
                                    //   /html/configuration_command_and_file_reference/core_server_configuration_reference#cnchangelog5-nsSymmetricKey
                                    //
                                    // Just show the encoded value at the moment.
                                    val = line.value.substring(3);
                                }
                            } else { // The value likely contains accented characters or has a trailing space.
                                val = b64DecodeUnicode(line.value.substring(3));
                            }
                        } else {
                            // Check for naming attribute
                            if (attr === rdnInfo.rdnAttr && val === rdnInfo.rdnVal) {
                                namingAttribute = true;
                            }
                        }
                        obj.id = generateUniqueId();
                        obj.attr = attr;
                        obj.val = val;
                        obj.namingAttr = namingAttribute;
                        obj.required = namingAttribute;
                        this.originalEntryRows.push(obj);
                    }
                });

                for (const ocRow of this.props.allObjectclasses) {
                    for (let row of this.originalEntryRows) {
                        if (ocRow.required.includes(row.attr) || row.attr == "dn") {
                            row.required = true;
                        }
                    }
                }

                // Update the editable rows.
                this.setState({
                    editableTableData: [...this.originalEntryRows],
                    objectclasses: objectclasses,
                });
        });
    }

    saveCurrentRows = (savedRows) => {
        this.setState({ savedRows });
    }

    updateValuesTableRows = () => {
        if (this.state.savedRows.length === 0) {
            return;
        }
        this.setState({
            editableTableData: [...this.state.savedRows]
        });
    };

    generateUpdateData = () => {
        const statementRows = [];
        const ldifArray = [];
        const updateArray = [];
        const addArray = [];
        const removeArray = [];
        let numOfChanges = 0;

        // Compare the saved rows with the original data.
        for (const originalRow of this.originalEntryRows) {
            // Check if the value has been changed by comparing
            // the unique IDs and the values.
            const matchingObj = this.state.savedRows.find(elt => (elt.id === originalRow.id));

            // Check if original row was removed
            if (!matchingObj) {
                removeArray.push(originalRow);
                continue;
            }

            // Now check the value.
            const sameValue = matchingObj.val === originalRow.val;
            if (sameValue) {
                updateArray.push({ ...originalRow });
            } else { // Value has changed.
                const myNewObject = {
                    ...originalRow,
                    op: LDAP_OPERATIONS.replace,
                    // old: originalRow.val,
                    new: matchingObj.val
                };

                if (matchingObj.encodedValue) {
                    myNewObject.encodedValue = matchingObj.encodedValue;
                }
                updateArray.push(myNewObject);
            }
        }

        // Check for new rows
        for (const savedRow of this.state.savedRows) {
            let found = false;
            for (const originalRow of this.originalEntryRows) {
                if (originalRow.id === savedRow.id) {
                    // Found, its not new
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Add new row
                addArray.push(savedRow);
            }
            found = false;
        }

        for (const datum of updateArray) {
            console.log("generateUpdateData replace: ", datum);
            const myAttr = datum.attr;
            const myVal = datum.val;
            if (myAttr === 'dn') { // Entry DN.
                ldifArray.push(`dn: ${myVal}`); // DN line.
                ldifArray.push('changetype: modify'); // To modify the entry.
            }
            if (datum.op === undefined) { // Unchanged value.
                statementRows.push({
                    cells: [
                        { title: (<Label>Keep</Label>) },
                        myAttr,
                        myVal
                    ]
                });
            } else { // Value was updated.
                if (ldifArray.length >= 4) { // There was already a first round of attribute replacement.
                    ldifArray.push('-');
                }

                const sameAttrArray = this.originalEntryRows.filter(obj => obj.attr === myAttr);
                // console.log('sameAttrArray');
                // console.log(sameAttrArray);
                const mySeparator = (BINARY_ATTRIBUTES.includes(myAttr.toLowerCase()))
                    ? '::'
                    : ':';

                if (sameAttrArray.length > 1) {
                    // The attribute has multiple values.
                    // We need to delete the specific value and add the new one.
                    ldifArray.push(`delete: ${myAttr}`);
                    ldifArray.push(`${myAttr}: ${myVal}`);
                    ldifArray.push('-');
                    ldifArray.push(`add: ${myAttr}`);
                } else {
                    // There is a single value for the attribute.
                    // A "replace" statement is enough.
                    ldifArray.push(`replace: ${myAttr}`);
                }

                const valueToUse = datum.encodedValue
                    ? datum.encodedValue
                    : datum.new;
                // foldLine() will return the line as is ( in an array though )
                // if its length is smaller than 78.
                // Otherwise the line is broken into smaller ones ( 78 characters max per line ).
                const remainingData = foldLine(`${myAttr}${mySeparator} ${valueToUse}`);
                // const remainingData = foldLine(`${myAttr}${mySeparator} ${datum.new}`);
                ldifArray.push(...remainingData);
                numOfChanges++;
                statementRows.push({
                    cells: [
                        { title: (<Label color="orange">Replace</Label>) },
                        myAttr,
                        {
                            title: (
                                <LabelGroup isVertical>
                                    <Label variant="outline" color="red">
                                        <em>old:</em>&ensp;{myVal}
                                    </Label>
                                    <Label variant="outline" color="blue" isTruncated>
                                        <em>new:</em>&ensp;{datum.new}
                                    </Label>
                                </LabelGroup>
                            )
                        }
                    ]
                });
            }
        } // End updateArray loop.

        // Loop add rows
        for (const datum of addArray) {
            const myAttr = datum.attr;
            const myVal = datum.val;
            console.log("generateUpdateData add: ", datum);
            numOfChanges++;
            // Update LDIF array
            if (ldifArray.length >= 4) { // There was already a first round of attribute replacement.
                ldifArray.push('-');
            }
            ldifArray.push('add: ' + myAttr);

            const remainingData = foldLine(`${myAttr}: ${myVal}`);
            ldifArray.push(...remainingData);

            // Update Table
            statementRows.push({
                    cells: [
                        { title: (<Label color="orange">Add</Label>) },
                        myAttr,
                        {
                            title: (
                                <Label variant="outline" color="blue" isTruncated>
                                    {myVal}
                                </Label>
                            )
                        }
                    ]
                });
        }

        // Loop delete rows
        for (const datum of removeArray) {
            const myAttr = datum.attr;
            const myVal = datum.val;
            console.log("generateUpdateData delete: ", datum);

            // Update LDIF array
            if (ldifArray.length >= 4) { // There was already a first round of attribute replacement.
                ldifArray.push('-');
            }
            ldifArray.push('delete: ' + myAttr);
            numOfChanges++;
            const remainingData = foldLine(`${myAttr}: ${myVal}`);
            ldifArray.push(...remainingData);

            // Update Table
            statementRows.push({
                    cells: [
                        { title: (<Label color="red">Delete</Label>) },
                        myAttr,
                        {
                            title: (
                                <Label variant="outline" color="blue" isTruncated>
                                    {myVal}
                                </Label>
                            )
                        }
                    ]
                });
        }

        this.setState({
            statementRows,
            ldifArray,
            numOfChanges: numOfChanges
        });
    }

    // Loop delete rows

    // Intentionally empty function.
    enableNextStep = (yes) => {
        this.setState({
            validMods: yes
        });
    };

    isAttributeSingleValued = (attr) => {
        return this.singleValuedAttributes.includes(attr.toLowerCase());
    };

    isAttributeRequired = attr => {
        return this.requiredAttributes.includes(attr);
    }

    render () {
        const {
            editableTableData,
            savedRows,
            statementRows,
            ldifArray,
            stepIdReached,
            resultVariant,
            commandOutput,
            validMods,
            numOfChanges
        } = this.state;

        const entryTableComponent = (
            <>
                <EditableTable
                    wizardEntryDn={this.props.wizardEntryDn}
                    editableTableData={editableTableData}
                    quickUpdate
                    isAttributeSingleValued={this.isAttributeSingleValued}
                    isAttributeRequired={this.isAttributeRequired}
                    enableNextStep={this.enableNextStep}
                    saveCurrentRows={this.saveCurrentRows}
                    allObjectclasses={this.props.allObjectclasses}
                />
            </>
        );

        const ldifListItems = ldifArray.map((line, index) =>
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
                        title="LDIF Statements"
                    />
                </div>
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
        const resultStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title="Result for User Modification"
                    className="ds-margin-bottom-md"
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

        const updateSteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 5,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Edit Values',
                component: entryTableComponent,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 5,
                enableNext: savedRows.length > 0 && validMods // savedRows is filled after a first edit.
            },
            {
                id: 3,
                name: 'View Changes',
                component: (
                    <Table
                        aria-label="Statement Table"
                        variant="compact"
                        cells={this.operationColumns}
                        rows={statementRows}
                    >
                        <TableHeader />
                        <TableBody />
                    </Table>
                ),
                canJumpTo: stepIdReached >= 3 && stepIdReached < 5,
                enableNext: numOfChanges > 0
            },
            {
                id: 4,
                name: 'LDIF Statements',
                component: ldifStatementsStep,
                nextButtonText: 'Apply',
                canJumpTo: stepIdReached >= 4 && stepIdReached < 5
            },
            {
                id: 5,
                name: 'Review Result',
                component: resultStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 5 && stepIdReached < 5,
                hideBackButton: true
            }
        ];

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                onNext={this.onNext}
                onBack={this.onBack}
                title="Edit LDAP Entry"
                description={`Entry DN: ${this.props.wizardEntryDn}`}
                steps={updateSteps}
            />
        );
    }
}

export default UpdateEntry;
