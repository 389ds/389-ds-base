import cockpit from "cockpit";
import React from 'react';
import {
    Alert,
    BadgeToggle,
    Card, CardBody, CardTitle,
    Dropdown, DropdownItem, DropdownPosition,
    Form,
    Grid, GridItem,
    Label,
    Pagination,
    SearchInput,
    Select, SelectOption, SelectVariant,
    SimpleList, SimpleListItem,
    Spinner,
    Text, TextContent, TextVariants,
    Wizard,
} from '@patternfly/react-core';
import {
    InfoCircleIcon,
} from '@patternfly/react-icons';
import {
    Table, TableHeader, TableBody, TableVariant,
    headerCol,
} from '@patternfly/react-table';
import EditableTable from '../../lib/editableTable.jsx';
import {
    createLdapEntry,
    foldLine,
    generateUniqueId
} from '../../lib/utils.jsx';
import {
    BINARY_ATTRIBUTES
} from '../../lib/constants.jsx';

const _ = cockpit.gettext;

class AddUser extends React.Component {
    constructor (props) {
        super(props);

        // Objectclasses
        this.userObjectclasses = {
            'Basic Account': [
                'objectclass: top',
                'objectClass: nsPerson',
                'objectClass: nsAccount',
                'objectClass: nsOrgPerson',
            ],
            'Posix Account': [
                'objectclass: top',
                'objectClass: nsPerson',
                'objectClass: nsAccount',
                'objectClass: nsOrgPerson',
                'objectClass: posixAccount',
            ],
            'Service Account': [
                'objectclass: top',
                'objectClass: applicationProcess',
                'objectClass: nsAccount'
            ],
        };

        this.allowedAttrs = {
            'Basic Account': [
                'businessCategory', 'carLicense', 'departmentNumber',
                'description', 'employeeNumber', 'employeeType', 'homePhone',
                'homePostalAddress', 'initials', 'jpegPhoto', 'labeledURI',
                'legalName', 'mail', 'manager', 'mobile', 'nsRoleDN', 'o',
                'pager', 'photo', 'preferredLanguage', 'nsCertSubjectDN',
                'nsSshPublicKey', 'roomNumber', 'seeAlso', 'telephoneNumber',
                'uid', 'userCertificate', 'userPassword', 'userSMIMECertificate',
                'userPKCS12', 'x500UniqueIdentifier'
            ],
            'Posix Account': [
                'businessCategory', 'carLicense', 'departmentNumber',
                'description', 'employeeNumber', 'employeeType', 'homePhone',
                'homePostalAddress', 'initials', 'jpegPhoto', 'labeledURI',
                'legalName', 'mail', 'manager', 'mobile', 'nsRoleDN', 'o',
                'pager', 'photo', 'preferredLanguage', 'nsCertSubjectDN',
                'nsSshPublicKey', 'roomNumber', 'seeAlso', 'telephoneNumber',
                'userCertificate', 'userPassword', 'userSMIMECertificate',
                'userPKCS12', 'x500UniqueIdentifier', 'loginShell', 'gecos'
            ],
            'Service Account': [
                'seeAlso', 'ou', 'l', 'description', 'userCertificate',
                'nsCertSubjectDN', 'nsRoleDN', 'nsSshPublicKey', 'userPassword'
            ],
        };

        this.requiredAttrs = {
            'Basic Account': [
                'cn', 'displayName',
            ],
            'Posix Account': [
                'cn', 'uid', 'uidNumber', 'gidNumber', 'homeDirectory',
                'displayName'
            ],
            'Service Account': [
                'cn'
            ]
        };

        this.singleValuedAttrs = {
            'Basic Account': [
                'preferredDeliveryMethod', 'displayName', 'employeeNumber',
                'preferredLanguage', 'userPassword',
            ],
            'Posix Account': [
                'preferredDeliveryMethod', 'displayName', 'employeeNumber',
                'preferredLanguage', 'userPassword', 'uidNumber', 'gidNumber',
                'homeDirectory', 'loginShell', 'gecos'
            ],
            'Service Account': [
                'userpassword'
            ]
        };

        this.attributeValidationRules = [
            {
                name: _("required"),
                validator: value => value.trim() !== '',
                errorText: _("This field is required")
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
            searchValue: "",
            stepIdReached: 1,
            itemCountAddUser: 0,
            pageAddUser: 1,
            perPageAddUser: 10,
            columnsUser: [
                { title: _("Attribute Name"), cellTransforms: [headerCol()] },
            ],
            rowsUser: [],
            rowsOrig: [],
            pagedRowsUser: [],
            selectedAttributes: [],
            isAttrDropDownOpen: false,
            // Values
            noEmptyValue: false,
            columnsValues: [
                _("Attribute"),
                _("Value")
            ],
            // Review step
            reviewValue: '',
            reviewInvalidText: _("Invalid LDIF"),
            reviewIsValid: true,
            reviewValidated: 'default',
            editableTableData: [],
            rdn: "",
            rdnValue: "",
            adding: true,
            accountType: "Basic Account",
            isOpenType: false,
        };

        this.handleNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            if (id === 4) {
                // Update the values table.
                this.updateValuesTableRows();
            } else if (id === 5) {
                // Generate the LDIF data.
                this.generateLdifData();
            } else if (id === 6) {
                // Create the LDAP entry.
                const myLdifArray = this.state.ldifArray;
                createLdapEntry(this.props.editorLdapServer,
                                myLdifArray,
                                (result) => {
                                    this.setState({
                                        commandOutput: result.errorCode === 0 ? _("Successfully added user!") : _("Failed to add user, error: ") + result.errorCode,
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
                                    };
                                    this.props.setWizardOperationInfo(opInfo);
                                }
                );
            }
        };

        this.handleBack = ({ id }) => {
            if (id === 4) {
                // true ==> Do not check the attribute selection when navigating back.
                this.updateValuesTableRows(true);
            }
        };

        this.handleToggleType = isOpenType => {
            this.setState({
                isOpenType
            });
        };
        this.handleSelectType = (event, selection) => {
            const attributesArray = [];
            let selectedAttrs = [];

            this.allowedAttrs[selection].map(attr => {
                attributesArray.push({ cells: [attr] });
                return [];
            });
            this.requiredAttrs[selection].map(attr => {
                attributesArray.push({
                    cells: [attr],
                    selected: true,
                    isAttributeSelected: true,
                    disableCheckbox: true
                });
                return [];
            });
            selectedAttrs = [...this.requiredAttrs[selection]];

            // Sort the attributes
            attributesArray.sort((a, b) => (a.cells[0] > b.cells[0]) ? 1 : -1);

            this.setState({
                itemCountAddUser: attributesArray.length,
                rowsUser: attributesArray,
                rowsOrig: [...attributesArray],
                pagedRowsUser: attributesArray.slice(0, this.state.perPageAddUser),
                accountType: selection,
                selectedAttributes: selectedAttrs,
                isOpenType: false,
            });
        };

        this.handleAttrSearchChange = (value) => {
            let attrRows = [];
            let allAttrs = [];
            const val = value.toLowerCase();

            allAttrs = this.state.rowsOrig;

            // Process search filter on the entire list
            if (val !== "") {
                for (const row of allAttrs) {
                    const name = row.cells[0].toLowerCase();
                    if (name.includes(val)) {
                        attrRows.push(row);
                    }
                }
            } else {
                // Restore entire row list
                attrRows = allAttrs;
            }

            this.setState({
                rowsUser: attrRows,
                pagedRowsUser: attrRows.slice(0, this.state.perPageAttr),
                itemCountAddUser: attrRows.length,
                searchValue: value
            });
        };

        // End constructor().
    }

    componentDidMount () {
        const attributesArray = [];
        // Set the default poisx user attributes
        this.allowedAttrs[this.state.accountType].map(attr => {
            attributesArray.push({ cells: [attr] });
            return [];
        });
        this.requiredAttrs[this.state.accountType].map(attr => {
            attributesArray.push({
                cells: [attr],
                selected: true,
                isAttributeSelected: true,
                disableCheckbox: true
            });
            return [];
        });

        // Sort the attributes
        attributesArray.sort((a, b) => (a.cells[0] > b.cells[0]) ? 1 : -1);

        this.setState({
            itemCountAddUser: attributesArray.length,
            rowsUser: attributesArray,
            rowsOrig: [...attributesArray],
            pagedRowsUser: attributesArray.slice(0, this.state.perPageAddUser),
            selectedAttributes: [...this.requiredAttrs[this.state.accountType]],
        });
    }

    handleSetPageAddUser = (_event, pageNumber) => {
        this.setState({
            pageAddUser: pageNumber,
            pagedRowsUser: this.getAttributesToShow(pageNumber, this.state.perPageAddUser)
        });
    };

    handlePerPageSelectAddUser = (_event, perPage) => {
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
        return this.singleValuedAttrs[this.state.accountType].includes(attr);
    };

    isAttributeRequired = attr => {
        return this.requiredAttrs[this.state.accountType].includes(attr);
    };

    handleSelect = (event, isSelected, rowId) => {
        // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
        if (this.state.pagedRowsUser[rowId].disableCheckbox === true) {
            return;
        } // End hack.

        // Process only the entries in the current page ( pagedRowsUser )
        const rows = [...this.state.pagedRowsUser];
        rows[rowId].selected = isSelected;
        // Find the entry in the full array and set 'isAttributeSelected' accordingly
        // The property 'isAttributeSelected' is used to build the LDAP entry to add.
        // The row ID cannot be used since it changes with the pagination.
        const attrName = rows[rowId].cells[0];
        const allItems = [...this.state.rowsUser];
        const allAttrs = this.state.rowsOrig;
        const index = allItems.findIndex(item => item.cells[0] === attrName);
        allItems[index].isAttributeSelected = isSelected;
        const selectedAttributes = allAttrs
                .filter(item => item.isAttributeSelected)
                .map(selectedAttr => selectedAttr.cells[0]);

        this.setState({
            rowsUser: allItems,
            pagedRowsUser: rows,
            selectedAttributes
        },
                      () => this.updateValuesTableRows());
    };

    updateValuesTableRows = (skipAttributeSelection) => {
        const newSelectedAttrs = [...this.state.selectedAttributes];
        let namingRowID = this.state.namingRowID;
        let namingAttrVal = this.state.namingAttrVal;
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
                };
                return obj;
            });
            editableTableData.sort((a, b) => (a.attr > b.attr) ? 1 : -1);
            namingRowID = editableTableData[0].id;
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
                    namingRowID = editableTableData[0].id;
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
            rows = this.state.editableTableData;
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
            <DropdownItem key={attr}>{attr}</DropdownItem>
        );

        return (
            <Dropdown
                className="ds-dropdown-padding"
                onSelect={this.handleAttrDropDownSelect}
                position={DropdownPosition.left}
                toggle={
                    <BadgeToggle id="toggle-attr-select" onToggle={this.handleAttrDropDownToggle}>
                        {numSelected !== 0 ? <>{numSelected} {_("selected")} </> : <>0 {_("selected")} </>}
                    </BadgeToggle>
                }
                isOpen={isAttrDropDownOpen}
                dropdownItems={items}
            />
        );
    };

    saveCurrentRows = (savedRows, namingID) => {
        this.setState({
            savedRows
        }, () => {
            // Update the naming information after the new rows have been saved.
            if (namingID !== -1) { // The namingIndex is set to -1 if the row is not the naming one.
                this.setNamingRowID(namingID);
            }
        });
    };

    generateLdifData = () => {
        const ldifArray = [];
        let isFilePath = false;
        const objectClassData = this.userObjectclasses[this.state.accountType];
        const cleanLdifArray = [];

        ldifArray.push(`dn: ${this.state.namingAttrVal},${this.props.wizardEntryDn}`);
        ldifArray.push(...objectClassData);

        for (const item of this.state.savedRows) {
            const attrName = item.attr;
            const attrVal = item.val;
            isFilePath = false;
            if ((typeof attrVal === 'string' || attrVal instanceof String) && (attrVal.toLowerCase().startsWith("file:/"))) {
                isFilePath = true;
            }
            const mySeparator = BINARY_ATTRIBUTES.includes(attrName.toLowerCase())
                ? (isFilePath ? ':<' : '::')
                : ':';

            const valueToUse = item.encodedvalue
                ? item.encodedvalue
                : attrVal;
            const remainingData = foldLine(`${attrName}${mySeparator} ${valueToUse}`);
            ldifArray.push(...remainingData);
            if (attrName.toLowerCase().startsWith("userpassword")) {
                cleanLdifArray.push("userpassword: ********");
            } else if (attrName.toLowerCase().startsWith("jpegphoto") && mySeparator === '::') {
                const myTruncatedValue = (
                    <div>
                        {"jpegphoto:: "}
                        <Label icon={<InfoCircleIcon />} color="blue">
                            {_("Value is too large to display")}
                        </Label>
                    </div>
                );
                cleanLdifArray.push(myTruncatedValue);
            } else {
                cleanLdifArray.push(...remainingData);
            }
        }

        this.setState({ ldifArray, cleanLdifArray });
    };

    render () {
        const {
            commandOutput, itemCountAddUser, pageAddUser, perPageAddUser,
            columnsUser, pagedRowsUser, ldifArray, cleanLdifArray, noEmptyValue,
            namingAttrVal, namingAttr, namingVal, resultVariant,
            editableTableData, stepIdReached
        } = this.state;

        const rdnValue = namingVal;
        const myTitle = (namingAttrVal === '' || rdnValue === '')
            ? _("Invalid Naming Attribute - Empty Value!")
            : _("DN ( Distinguished Name )");

        const userSelectStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Select Entry Type")}
                        </Text>
                    </TextContent>
                </div>
                <div className="ds-indent">
                    <Select
                        variant={SelectVariant.single}
                        className="ds-margin-top-lg"
                        aria-label="Select user type"
                        onToggle={this.handleToggleType}
                        onSelect={this.handleSelectType}
                        selections={this.state.accountType}
                        isOpen={this.state.isOpenType}
                    >
                        <SelectOption key="user" value="Basic Account" />
                        <SelectOption key="posix" value="Posix Account" />
                        <SelectOption key="service" value="Service Account" />
                    </Select>
                    <TextContent className="ds-margin-top-xlg">
                        <Text component={TextVariants.h6} className="ds-margin-top-lg ds-font-size-md">
                            <b>{_("Basic Account")}</b>{_(" - This type of user entry uses a common set of objectclasses (nsPerson, nsAccount, and nsOrgPerson).")}
                        </Text>
                        <Text component={TextVariants.h6} className="ds-margin-top-lg ds-font-size-md">
                            <b>{_("Posix Account")}</b>{_(" - This type of user entry uses a similar set of objectclasses as the ")}<i>{_("Basic Account")}</i> {_("(nsPerson, nsAccount, nsOrgPerson, and posixAccount), but it includes POSIX attributes like:")}
                            <i>{_("uidNumber, gidNumber, homeDirectory, loginShell, and gecos")}</i>.
                        </Text>
                        <Text component={TextVariants.h6} className="ds-margin-top-lg ds-font-size-md">
                            <b>{_("Service Account")}</b>{_(" - This type of entry uses a bare minimum of objectclasses (nsAccount, and applicationProcess) and attributes to create a simple object used to represent a service (not a user identity).")}
                        </Text>
                    </TextContent>
                </div>
            </>
        );

        const userAttributesStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Select Entry Attributes")}
                        </Text>
                    </TextContent>
                    {this.buildAttrDropdown()}
                </div>
                <Grid className="ds-margin-top-lg">
                    <GridItem span={5}>
                        <SearchInput
                            className="ds-font-size-md"
                            placeholder={_("Search Attributes")}
                            value={this.state.searchValue}
                            onChange={(evt, val) => this.handleAttrSearchChange(val)}
                            onClear={() => this.handleAttrSearchChange('')}
                        />
                    </GridItem>
                    <GridItem span={7}>
                        <Pagination
                            itemCount={itemCountAddUser}
                            page={pageAddUser}
                            perPage={perPageAddUser}
                            onSetPage={this.handleSetPageAddUser}
                            widgetId="pagination-options-menu-add-user"
                            onPerPageSelect={this.handlePerPageSelectAddUser}
                            isCompact
                        />
                    </GridItem>
                </Grid>
                <Table
                    cells={columnsUser}
                    rows={pagedRowsUser}
                    onSelect={this.handleSelect}
                    variant={TableVariant.compact}
                    aria-label="Pagination User Attributes"
                    canSelectAll={false}
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </>
        );

        const userValuesStep = (
            <>
                <Form autoComplete="off">
                    <Grid>
                        <GridItem className="ds-margin-top" span={12}>
                            <Alert
                                variant={namingAttr === '' || namingVal === ''
                                    ? "warning"
                                    : "success"}
                                isInline
                                title={myTitle}
                            >
                                <b>{_("Entry DN:")}&nbsp;&nbsp;&nbsp;</b>{(namingAttr || "??????")}={namingVal || "??????"},{this.props.wizardEntryDn}
                            </Alert>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    {_("Set Attribute Values")}
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                </Form>
                <GridItem className="ds-left-margin" span={11}>
                    <EditableTable
                        key={editableTableData}
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
            </>
        );

        const ldifListItems = cleanLdifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={(typeof line === 'string' || line instanceof String) && line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const userCreationStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title={_("LDIF Content for User Creation")}
                />
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
        const userReviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title={_("Result for User Creation")}
                >
                    {commandOutput}
                    {this.state.adding &&
                        <div>
                            <Spinner className="ds-left-margin" size="md" />
                            &nbsp;&nbsp;{_("Adding user ...")}
                        </div>}
                </Alert>
                {resultVariant === 'danger' &&
                    <Card isSelectable>
                        <CardTitle>
                            {_("LDIF Data")}
                        </CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>}
            </div>
        );

        const addUserSteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 6,
                hideBackButton: true
            },
            {
                id: 2,
                name: _("Choose User Type"),
                component: userSelectStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 6
            },
            {
                id: 3,
                name: _("Select Attributes"),
                component: userAttributesStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 6
            },
            {
                id: 4,
                name: _("Set Values"),
                component: userValuesStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 6,
                enableNext: noEmptyValue
            },
            {
                id: 5,
                name: _("Create User"),
                component: userCreationStep,
                nextButtonText: _("Create User"),
                canJumpTo: stepIdReached >= 5 && stepIdReached < 6
            },
            {
                id: 6,
                name: _("Review Result"),
                component: userReviewStep,
                nextButtonText: _("Finish"),
                canJumpTo: stepIdReached >= 6,
                hideBackButton: true,
                enableNext: !this.state.adding
            }
        ];

        const title = (
            <>
                {_("Parent DN: ")}&nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
            </>
        );

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.handleToggleWizard}
                onNext={this.handleNext}
                onBack={this.handleBack}
                title={_("Add A User")}
                description={title}
                steps={addUserSteps}
            />
        );
    }
}

export default AddUser;
