import React from 'react';
import {
    Alert,
    BadgeToggle,
    Button,
    Card,
    CardBody,
    CardTitle,
    Dropdown,
    DropdownItem,
    DropdownPosition,
    DualListSelector,
    Form,
    Grid,
    GridItem,
    Label,
    Modal,
    ModalVariant,
    Pagination,
    Radio,
    SearchInput,
    SimpleList,
    SimpleListItem,
    Spinner,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    ValidatedOptions,
    Wizard,
} from '@patternfly/react-core';
import {
    Table,
    TableHeader,
    TableBody,
    TableVariant,
    headerCol,
} from '@patternfly/react-table';
import EditableTable from '../../lib/editableTable.jsx';
import LdapNavigator from '../../lib/ldapNavigator.jsx';
import {
    createLdapEntry,
    runGenericSearch,
    generateUniqueId
} from '../../lib/utils.jsx';
import {
    InfoCircleIcon
} from '@patternfly/react-icons';


class AddRole extends React.Component {
    constructor (props) {
        super(props);

        this.roleDefinitionArray = ['description', 'nsRoleScopeDN'];
        this.filteredRoleDefinitionArray = ['nsRoleFilter'];
        this.nestedRoleDefinitionArray = ['nsRoleDN'];
        this.singleValuedAttributes = ['nsRoleFilter', 'nsRoleDN', 'nsRoleScopeDN'];

        this.requiredAttributes = ['cn', 'nsRoleFilter', 'nsRoleDN'];

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
            itemCountAddRole: 0,
            pageAddRole: 1,
            perpageAddRole: 10,
            columnsRole: [
                { title: 'Attribute Name', cellTransforms: [headerCol()] },
                { title: 'From ObjectClass' }
            ],
            rowsRole: [],
            pagedRowsRole: [],
            selectedAttributes: ['cn'],
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
            parentDN: "",
            isTreeLoading: false,
            roleType: "managed",
            roleDNs: [],
            searching: false,
            searchPattern: "",
            showLDAPNavModal: false,
            rolesSearchBaseDn: "",
            rolesAvailableOptions: [],
            rolesChosenOptions: [],
            adding: true,
        };

        this.handleBaseDnSelection = (treeViewItem) => {
            this.setState({
                rolesSearchBaseDn: treeViewItem.dn
            });
        }

        this.showTreeLoadingState = (isTreeLoading) => {
            this.setState({
                isTreeLoading,
                searching: isTreeLoading ? true : false
            });
        }

        this.openLDAPNavModal = () => {
            this.setState({
                showLDAPNavModal: true
            });
        };

        this.closeLDAPNavModal = () => {
            this.setState({
                showLDAPNavModal: false
            });
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            if (id === 4) {
                // Update the attributes table.
                this.updateAttributesTableRows();
                this.updateValuesTableRows();
            } else if (id === 5) {
                // Update the values table.
                this.updateValuesTableRows();
            } else if (id === 6) {
                // Generate the LDIF data.
                this.generateLdifData();
            } else if (id === 7) {
                // Create the LDAP entry.
                const myLdifArray = this.state.ldifArray;
                createLdapEntry(this.props.editorLdapServer,
                    myLdifArray,
                    (result) => {
                        this.setState({
                            commandOutput: result.errorCode === 0 ? 'Role successfully created!' : 'Failed to create role, error: ' + result.errorCode,
                            resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                            adding: false,
                        }, () => {
                            this.props.onReload();
                        });
                        // Update the wizard operation information.
                        const myDn = myLdifArray[0].substring(4);
                        const relativeDn = myLdifArray[6].replace(": ", "="); // cn val
                        const opInfo = {
                            operationType: 'ADD',
                            resultCode: result.errorCode,
                            time: Date.now(),
                            entryDn: myDn,
                            relativeDn: relativeDn
                        }
                        this.props.setWizardOperationInfo(opInfo);
                    }
                );
            }
        };

        this.onBack = ({ id }) => {
            if (id === 5) {
                // true ==> Do not check the attribute selection when navigating back.
                this.updateValuesTableRows(true);
            }
        };

        this.handleSearchClick = () => {
            this.setState({
                isSearchRunning: true,
                rolesAvailableOptions: []
            }, () => { this.getEntries () });
        };

        this.handleSearchPattern = searchPattern => {
            this.setState({ searchPattern });
        };

        this.getEntries = () => {
            const baseDn = this.state.rolesSearchBaseDn;
            const pattern = this.state.searchPattern;
            const filter = pattern === ''
                ? '(|(objectClass=LDAPsubentry)(objectClass=nsRoleDefinition))'
                : `(&(|(objectClass=LDAPsubentry)(objectClass=nsRoleDefinition)(objectClass=nsSimpleRoleDefinition)(objectClass=nsComplexRoleDefinition)(objectClass=nsManagedRoleDefinition)(objectClass=nsFilteredRoleDefinition)(objectClass=nsNestedRoleDefinition))(|(cn=*${pattern}*)(uid=${pattern})))`;
            const attrs = 'cn';

            const params = {
                serverId: this.props.editorLdapServer,
                baseDn: baseDn,
                scope: 'sub',
                filter: filter,
                attributes: attrs
            };
            runGenericSearch(params, (resultArray) => {
                const newOptionsArray = resultArray.map(result => {
                    const lines = result.split('\n');
                    // TODO: Currently picking the first value found.
                    // Might be worth to take the value that is used as RDN in case of multiple values.

                    // Handle base64-encoded data:
                    const pos0 = lines[0].indexOf(':: ');
                    const pos1 = lines[1].indexOf(':: ');

                    let dnLine = lines[0];
                    if (pos0 > 0) {
                        const decoded = decodeLine(dnLine);
                        dnLine = `${decoded[0]}: ${decoded[1]}`;
                    }
                    const value = pos1 === -1
                    ? (lines[1]).split(': ')[1]
                    : decodeLine(lines[1])[1];

                    return (
                        <span title={dnLine}>
                            {value}
                        </span>
                    );
                });

                this.setState({
                    rolesAvailableOptions: newOptionsArray,
                    isSearchRunning: false
                });
            });
        }

        this.removeDuplicates = (options) => {
            const titles = options.map(item => item.props.title);
            const noDuplicates = options
                .filter((item, index) => {
                    return titles.indexOf(item.props.title) === index;
                });
            return noDuplicates;
        };

        this.usersOnListChange = (newAvailableOptions, newChosenOptions) => {
            const newAvailNoDups = this.removeDuplicates(newAvailableOptions);
            const newChosenNoDups = this.removeDuplicates(newChosenOptions);

            this.setState({
                rolesAvailableOptions: newAvailNoDups.sort(),
                rolesChosenOptions: newChosenNoDups.sort()
            });

        };

        this.handleRadioChange = (_, event) => {
            this.setState({
                roleType: event.currentTarget.id,
            });
        };

        this.handleChange = this.handleChange.bind(this);
    }

    updateAttributesTableRows () {
        let attributesArray = [];
        attributesArray.push({
            cells: ['cn', 'LdapSubEntry'],
            selected: true,
            isAttributeSelected: true,
            disableCheckbox: true
        });

        this.roleDefinitionArray.map(attr => {
            attributesArray.push({ cells: [attr, 'nsRoleDefinition'] });
        });

        if (this.state.roleType === 'filtered') {
            this.filteredRoleDefinitionArray.map(attr => {
                attributesArray.push({ cells: [attr, 'nsFilteredRoleDefinition'],
                                       selected: true,
                                       isAttributeSelected: true,
                                       disableCheckbox: true
                                    });
            });
        } else if (this.state.roleType === 'nested') {
            this.nestedRoleDefinitionArray.map(attr => {
                attributesArray.push({ cells: [attr, 'nsNestedRoleDefinition'],
                                       selected: true,
                                       isAttributeSelected: true,
                                       disableCheckbox: true
                                    });
            });
        }

        // Sort the attributes
        attributesArray.sort((a, b) => (a.cells[0] > b.cells[0]) ? 1 : -1);

        this.setState({
            selectedAttributes: ['cn',
                                ...(this.state.roleType === 'filtered' ? ['nsRoleFilter'] : []),
                                ...(this.state.roleType === 'nested' ? ['nsRoleDN'] : [])],
            itemCountAddRole: attributesArray.length,
            rowsRole: attributesArray,
            pagedRowsRole: attributesArray.slice(0, this.state.perpageAddRole),
            parentDN: this.props.wizardEntryDn,
            rolesSearchBaseDn: this.props.wizardEntryDn
        });
    }

    componentDidMount () {
        this.setState({
            parentDN: this.props.wizardEntryDn,
            rolesSearchBaseDn: this.props.wizardEntryDn
        });
    }

    onSetpageAddRole = (_event, pageNumber) => {
        this.setState({
            pageAddRole: pageNumber,
            pagedRowsRole: this.getAttributesToShow(pageNumber, this.state.perpageAddRole)
        });
    };

    onPerPageSelectAddRole = (_event, perPage) => {
        this.setState({
            pageAddRole: 1,
            perpageAddRole: perPage,
            pagedRowsRole: this.getAttributesToShow(1, perPage)
        });
    };

    getAttributesToShow (page, perPage) {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        const newRows = this.state.rowsRole.slice(start, end);
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
        let selectedAttributes;
        if (rowId === -1) {
            // Process the full table entries ( rowsRole )
            rows = this.state.rowsRole.map(oneRow => {
                oneRow.selected = isSelected;
                oneRow.isAttributeSelected = isSelected;
                return oneRow;
            });
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            // Both 'cn' and 'sn' ( first 2 elements in the table ) are mandatory.
            // TODO: https://www.patternfly.org/v4/components/table#selectable
            // ==> disableSelection: true
            rows[0].selected = true;
            rows[0].isAttributeSelected = true;
            selectedAttributes = ['cn'];
            if (this.state.roleType === 'filtered') {
                selectedAttributes.push('nsRoleFilter');
                rows[1].selected = true;
                rows[1].isAttributeSelected = true;
            } else if (this.state.roleType === 'nested') {
                selectedAttributes.push('nsRoleDN');
                rows[1].selected = true;
                rows[1].isAttributeSelected = true;
            }
            this.setState({
                rowsRole: rows,
                allAttributesSelected: isSelected,
                selectedAttributes
            },
            () => {
                this.setState({
                    pagedRowsRole: this.getAttributesToShow(this.state.pageAddRole, this.state.perpageAddRole)
                });
                this.updateValuesTableRows();
            });
        } else {
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            if (this.state.pagedRowsRole[rowId].disableCheckbox === true) {
                return;
            } // End hack.

            // Process only the entries in the current page ( pagedRowsRole )
            rows = [...this.state.pagedRowsRole];
            rows[rowId].selected = isSelected;
            // Find the entry in the full array and set 'isAttributeSelected' accordingly
            // The property 'isAttributeSelected' is used to build the LDAP entry to add.
            // The row ID cannot be used since it changes with the pagination.
            const attrName = this.state.pagedRowsRole[rowId].cells[0];
            let allItems = [...this.state.rowsRole];
            const index = allItems.findIndex(item => item.cells[0] === attrName);
            allItems[index].isAttributeSelected = isSelected;
            const selectedAttributes = allItems
                .filter(item => item.isAttributeSelected)
                .map(selectedAttr => selectedAttr.cells[0]);

            this.setState({
                rowsRole: allItems,
                pagedRowsRole: rows,
                selectedAttributes
            },
            () => this.updateValuesTableRows());
        }
    };

    updateValuesTableRows = (skipAttributeSelection) => {
        const newSelectedAttrs = this.state.allAttributesSelected
            ? ['cn',
            ...this.roleDefinitionArray,
            ...this.filteredRoleDefinitionArray,
            ...this.nestedRoleDefinitionArray]
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
                    val: namingVal ? namingVal : '',
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
            if (this.state.roleType === 'nested') {
                for (const userObj of this.state.rolesChosenOptions) {
                    const dn_val = userObj.props.title.replace(/^dn: /, "");
                    editableTableData = editableTableData.filter((item) => ((item.attr.toLowerCase() !== 'nsroledn') || (item.val !== dn_val)));
                    editableTableData.push({id: generateUniqueId(),
                                            attr: 'nsRoleDN',
                                            val: dn_val,
                                            required: true,
                                            namingAttr: false,
                    });
                }
            }
            this.setState({
                savedRows: editableTableData
            });
        } else {
            if (skipAttributeSelection) { // Do not check the attribute selection ( because it has not changed ).
                editableTableData = [...this.state.savedRows];
            } else {
                let arrayOfAttrObjects = [...this.state.savedRows];
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
            if (this.state.roleType === 'nested') {
                for (const userObj of this.state.rolesChosenOptions) {
                    const dn_val = userObj.props.title.replace(/^dn: /, "");
                    editableTableData = editableTableData.filter((item) => ((item.attr.toLowerCase() !== 'nsroledn') || (item.val !== dn_val)));
                    editableTableData.push({id: generateUniqueId(),
                                            attr: 'nsRoleDN',
                                            val: dn_val,
                                            required: true,
                                            namingAttr: false,
                    });
                }
            }
            this.setState({
                savedRows: editableTableData
            });
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
        let objectClassData = ['ObjectClass: top',
                                 'ObjectClass: LdapSubEntry',
                                 'ObjectClass: nsRoleDefinition'];
        if (this.state.roleType === 'managed') {
            objectClassData.push('ObjectClass: nsSimpleRoleDefinition',
                                'ObjectClass: nsManagedRoleDefinition');
        } else if (this.state.roleType === 'filtered') {
            objectClassData.push('ObjectClass: nsComplexRoleDefinition',
                                'ObjectClass: nsFilteredRoleDefinition');
        } else if (this.state.roleType === 'nested') {
            objectClassData.push('ObjectClass: nsComplexRoleDefinition',
                                'ObjectClass: nsNestedRoleDefinition');
        }

        let valueData = [];
        for (const item of this.state.savedRows) {
            const attrName = item.attr;
            valueData.push(`${attrName}: ${item.val}`);
            if (objectClassData.length === 5) { // There will a maximum of 5 ObjectClasses.
                continue;
            }
            // TODO: Find a better logic!
            //if ((!objectClassData.includes('ObjectClass: InetOrgPerson')) &&
            //this.inetorgPersonArray.includes(attrName)) {
            //    objectClassData.push('ObjectClass: InetOrgPerson');
            //}
            //if (!objectClassData.includes('ObjectClass: OrganizationalPerson') &&
            //this.organizationalPersonArray.includes(attrName)) {
            //    objectClassData.push('ObjectClass: OrganizationalPerson');
            //}
        }

        const ldifArray = [
            `dn: ${this.state.namingAttrVal},${this.props.wizardEntryDn}`,
            ...objectClassData,
            ...valueData
        ];

        this.setState({ ldifArray });
    }

    handleChange (e) {
        const attr = e.target.id;
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [attr]: value,
        })
    }

    render () {
        const {
            itemCountAddRole, pageAddRole, perpageAddRole, columnsRole, pagedRowsRole,
            ldifArray, cleanLdifArray, columnsValues, noEmptyValue, alertVariant,
            namingAttrVal, namingAttr, resultVariant, editableTableData,
            stepIdReached, namingVal, rolesSearchBaseDn, rolesAvailableOptions,
            rolesChosenOptions, showLDAPNavModal, commandOutput, roleType
        } = this.state;

        const rdnValue = namingVal;
        const myTitle = (namingAttrVal === '' || rdnValue === '')
            ? 'Invalid Naming Attribute - Empty Value!'
            : 'DN ( Distinguished Name )';

        const namingValAndTypeStep = (
            <div>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Select Name And Role Type
                    </Text>
                </TextContent>
                <Form autoComplete="off">
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={2} className="ds-label">
                            Role Name
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                value={namingVal}
                                type="text"
                                id="namingVal"
                                aria-describedby="namingVal"
                                name="namingVal"
                                onChange={(str, e) => {
                                    this.handleChange(e);
                                }}
                                validated={this.state.namingVal === '' ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>

                    <TextContent className="ds-margin-top-lg">
                        <Text component={TextVariants.h5}>
                            Choose Role Type
                        </Text>
                    </TextContent>
                    <div className="ds-left-margin">
                        <Radio
                            name="roleType"
                            id="managed"
                            value="managed"
                            label="Managed"
                            isChecked={this.state.roleType === 'managed'}
                            onChange={this.handleRadioChange}
                            description="This attribute uses objectclass 'RoleOfNames'"
                        />
                        <Radio
                            name="roleType"
                            id="filtered"
                            value="filtered"
                            label="Filtered"
                            isChecked={this.state.roleType === 'filtered'}
                            onChange={this.handleRadioChange}
                            description="This attribute uses objectclass 'RoleOfUniqueNames'"
                            className="ds-margin-top"
                        />
                        <Radio
                            name="roleType"
                            id="nested"
                            value="nested"
                            label="Nested"
                            isChecked={this.state.roleType === 'nested'}
                            onChange={this.handleRadioChange}
                            description="This attribute uses objectclass 'RoleOfUniqueNames'"
                            className="ds-margin-top"
                        />
                    </div>
                </Form>
            </div>
        );

        const addRolesStep = (
            <React.Fragment>
                <Form autoComplete="off">
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Add Roles to the Nested Role
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <TextContent>
                                <Text>
                                    Search Base:
                                    <Text
                                        className="ds-left-margin"
                                        component={TextVariants.a}
                                        onClick={this.openLDAPNavModal}
                                        href="#"
                                    >
                                        {rolesSearchBaseDn}
                                    </Text>
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top">
                            <SearchInput
                            placeholder="Find roles..."
                            value={this.state.searchPattern}
                            onChange={this.handleSearchPattern}
                            onSearch={this.handleSearchClick}
                            onClear={() => { this.handleSearchPattern('') }}
                            />
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <DualListSelector
                                availableOptions={rolesAvailableOptions}
                                chosenOptions={rolesChosenOptions}
                                availableOptionsTitle="Available Roles"
                                chosenOptionsTitle="Chosen Roles"
                                onListChange={this.usersOnListChange}
                                id="usersSelector"
                            />
                        </GridItem>

                        <Modal
                            variant={ModalVariant.medium}
                            title="Choose A Branch To Search"
                            isOpen={showLDAPNavModal}
                            onClose={this.closeLDAPNavModal}
                            actions={[
                                <Button
                                    key="confirm"
                                    variant="primary"
                                    onClick={this.closeLDAPNavModal}
                                >
                                    Done
                                </Button>
                            ]}
                        >
                            <Card isSelectable className="ds-indent ds-margin-bottom-md">
                                <CardBody>
                                    <LdapNavigator
                                        treeItems={[...this.props.treeViewRootSuffixes]}
                                        editorLdapServer={this.props.editorLdapServer}
                                        skipLeafEntries={true}
                                        handleNodeOnClick={this.handleBaseDnSelection}
                                        showTreeLoadingState={this.showTreeLoadingState}
                                    />
                                </CardBody>
                            </Card>
                        </Modal>
                    </Grid>
                </Form>
            </React.Fragment>
        );

        const roleAttributesStep = (
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
                    itemCount={itemCountAddRole}
                    page={pageAddRole}
                    perPage={perpageAddRole}
                    onSetPage={this.onSetpageAddRole}
                    widgetId="pagination-options-menu-add-role"
                    onPerPageSelect={this.onPerPageSelectAddRole}
                    isCompact
                />
                <Table
                    cells={columnsRole}
                    rows={pagedRowsRole}
                    onSelect={this.onSelect}
                    variant={TableVariant.compact}
                    aria-label="Pagination Role Attributes"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </>
        );

        const roleValuesStep = (
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

        const ldifListItems = ldifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const roleCreationStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title="LDIF Content for Role Creation"
                />
                <Card isSelectable>
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
        const roleReviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title="Result for Role Creation"
                >
                    {commandOutput}
                    {this.state.adding &&
                        <div>
                            <Spinner className="ds-left-margin" size="md" />
                            &nbsp;&nbsp;Adding Role ...
                        </div>
                    }
                </Alert>
                {resultVariant === 'danger' &&
                    <Card isSelectable>
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

        const addRoleSteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 7,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Select Name & Type',
                component: namingValAndTypeStep,
                enableNext: namingVal === '' ? false : true,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 7,
            },
            ...(roleType === 'nested' ? [
                {
                    id: 3,
                    name: 'Add Nested Roles',
                    component: addRolesStep,
                    canJumpTo: stepIdReached >= 3 && stepIdReached < 7
                },
            ] : []),
            {
                id: 4,
                name: 'Select Attributes',
                component: roleAttributesStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 7
            },
            {
                id: 5,
                name: 'Set Values',
                component: roleValuesStep,
                canJumpTo: stepIdReached >= 5 && stepIdReached < 7,
                enableNext: noEmptyValue
            },
            {
                id: 6,
                name: 'Create Role',
                component: roleCreationStep,
                nextButtonText: 'Create',
                canJumpTo: stepIdReached >= 6 && stepIdReached < 7
            },
            {
                id: 7,
                name: 'Review Result',
                component: roleReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 7,
                hideBackButton: true,
                enableNext: !this.state.adding
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
                title="Add A Role"
                description={title}
                steps={addRoleSteps}
            />
        );
    }
}

export default AddRole;
