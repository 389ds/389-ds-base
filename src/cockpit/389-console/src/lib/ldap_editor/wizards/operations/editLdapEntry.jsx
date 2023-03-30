import React from 'react';
import {
    Alert,
    BadgeToggle,
    Bullseye,
    Card, CardHeader, CardBody, CardTitle,
    Dropdown, DropdownItem, DropdownPosition,
    Grid, GridItem,
    Label, LabelGroup,
    Pagination,
    SearchInput,
    SimpleList, SimpleListItem,
    Spinner,
    Text, TextContent, TextVariants,
    Title,
    Wizard,
} from '@patternfly/react-core';
import {
    InfoCircleIcon,
} from '@patternfly/react-icons';
import {
    Table, TableHeader, TableBody, TableVariant,
    breakWord,
    headerCol,
} from '@patternfly/react-table';
import {
    b64DecodeUnicode,
    foldLine,
    getBaseLevelEntryAttributes,
    getRdnInfo,
    generateUniqueId,
    getSingleValuedAttributes,
    modifyLdapEntry,
} from '../../lib/utils.jsx';
import EditableTable from '../../lib/editableTable.jsx';
import EditGroup from './editGroup.jsx';
import {
    LDAP_OPERATIONS,
    BINARY_ATTRIBUTES,
    LDIF_MAX_CHAR_PER_LINE
} from '../../lib/constants.jsx';

class EditLdapEntry extends React.Component {
    constructor (props) {
        super(props);

        this.originalEntryRows = [];
        this.singleValuedAttributes = [];
        this.requiredAttributes = ['dn'];
        this.operationColumns = [
            { title: 'Statement' },
            { title: 'Attribute' },
            { title: 'Value', cellTransforms: [breakWord] }
        ];

        this.state = {
            loading: true,
            isOCDropDownOpen: false,
            isAttrDropDownOpen: false,
            namingAttributeData: ['', ''],
            namingAttrPropsName: '',
            namingRowIndex: -1,
            namingAttribute: '',
            namingValue: '',
            editableTableData: [],
            statementRows: [],
            ldifArray: [],
            cleanLdifArray: [],
            validMods: false,
            commandOutput: '',
            resultVariant: 'default',
            stepIdReached: 1,
            itemCountOc: 0,
            pageOc: 1,
            perPageOc: 6,
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
            pagedRowsOc: [],
            pagedRowsAttr: [],
            selectedObjectClasses: [],
            selectedAttributes: [],
            attrsToRemove: [],
            modifying: true,
            isGroupOfNames: false,
            isGroupOfUniqueNames: false,
            groupMembers: [],
            groupGenericEditor: false,
            localProps: {...this.props},
            searchOCValue: "",
            searchValue: "",
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            // The function updateValuesTableRows() is called upon new seletion(s)
            // Make sure the values table is updated in case no selection was made.
            if (id === 2) {
                // Just call this function in order to make sure the values table is up-to-date
                // even after navigating back and forth.
                this.updateAttributeTableRows();
            } else if (id === 3) {
                // Remove attributes from removed objectclasses
                this.cleanUpEntry();
            } else if (id === 4) {
                // Generate the LDIF data at step 4.
                this.generateLdifData();
            } else if (id === 6) {
                const params = { serverId: this.state.localProps.editorLdapServer };
                modifyLdapEntry(params, this.state.ldifArray, (result) => {
                    if (result.errorCode === 0) {
                        result.output = "Successfully modified entry"
                    }
                    this.setState({
                        commandOutput: result.output,
                        commandOutput: result.errorCode === 0 ? 'Successfully modified entry!' : 'Failed to modify entry, error: ' + result.errorCode ,
                        resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                        modifying: false,
                    }, () => { this.state.localProps.onReload() }); // refreshes tableView
                    const opInfo = { // This is what refreshes treeView
                        operationType: 'MODIFY',
                        resultCode: result.errorCode,
                        time: Date.now()
                    }
                    this.state.localProps.setWizardOperationInfo(opInfo);
                });
            }
        };

        this.cleanUpEntry = () => {
            let newRows = [];
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
        }

        this.onOCSearchChange = (value, event) => {
            let ocRows = [];
            let allOCs = [];
            const val = value.toLowerCase();

            // Get fresh list of Objectclasses and what is selected
            this.state.localProps.allObjectclasses.map(oc => {
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
                }
                allOCs.push(
                    {
                        cells: [
                            oc.name,
                            oc.required.join(', '),
                            oc.optional.join(', '),
                        ],
                        selected: selected,
                        disableSelection: selectionDisabled
                    });
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
                searchOCValue: value
            })
        }

        this.onAttrSearchChange = (value, event) => {
            let attrRows = [];
            let allAttrs = [];
            const val = value.toLowerCase();

            allAttrs = this.getAllAttrs();

            // Process search filter on the entire list
            if (val !== "") {
                for (const row of allAttrs) {
                    const name = row.attributeName.toLowerCase();
                    const oc = row.cells[1].toLowerCase();
                    if (name.includes(val) || oc.includes(val)) {
                        attrRows.push(row);
                    }
                }
            } else {
                // Restore entire rowsAttr list
                attrRows = allAttrs;
            }

            this.setState({
                rowsAttr: attrRows,
                pagedRowsAttr: attrRows.slice(0, this.state.perPageAttr),
                itemCountAttr: attrRows.length,
                searchValue: value,
            })
        }

        this.useGroupGenericEditor = this.useGroupGenericEditor.bind(this);
        this.loadEntry = this.loadEntry.bind(this);
        // End constructor().
    }

    useGroupGenericEditor = () => {
        this.originalEntryRows = [];
        this.setState({
            groupGenericEditor: true
        });
    }

    openEditEntry = (dn) => {
        // used by group modal
        let editProps = { ...this.state.localProps};
        editProps.wizardEntryDn = dn;
        this.setState({
            localProps: editProps,
            isGroupOfNames: false,
            isGroupOfUniqueNames: false,
        })
    }

    isAttributeSingleValued = (attr) => {
        return this.singleValuedAttributes.includes(attr.toLowerCase());
    };

    isAttributeRequired = attr => {
        return this.requiredAttributes.includes(attr);
    }

    enableNextStep = (yes) => {
        this.setState({
            validMods: yes
        });
    };

    saveCurrentRows = (editableTableData) => {
        let validMods = true;
        for (const row of editableTableData) {
            if (row.val === "") {
                validMods = false;
                break;
            }
        }

        this.setState({
            editableTableData,
            validMods
        });
    }

    loadEntry(reload) {
        const ocArray = [];
        if (reload) {
            this.originalEntryRows = [];
        }

        getBaseLevelEntryAttributes(this.state.localProps.editorLdapServer,
            this.state.localProps.wizardEntryDn,
            (entryDetails) => {
                let objectclasses = [];
                const rdnInfo = getRdnInfo(this.state.localProps.wizardEntryDn);
                let namingAttr = "";
                let namingValue = "";
                let namingIndex = -1;
                let attrPropsName = "";
                let isGroupOfUniqueNames = false;
                let isGroupOfNames = false;
                let members = [];

                entryDetails
                .filter(data => (data.attribute + data.value !== '' && // Filter out empty lines
                data.attribute !== '???: ')) // and data for empty suffix(es) and in case of failure.
                .map((line, index) => {
                    let attrLowerCase;
                    let namingAttribute = false;
                    if (line.attribute !== undefined) {
                        const obj = {};
                        const attr = line.attribute;
                        attrLowerCase = attr.trim().toLowerCase();
                        let val = line.value.substring(1).trim();
                        let encodedvalue = "";

                        if (attrLowerCase === "objectclass") {
                            objectclasses.push(val);
                            if (val.toLowerCase() === "groupofnames") {
                                isGroupOfNames = true;
                            } else if (val.toLowerCase() === "groupofuniquenames") {
                                isGroupOfUniqueNames = true;
                            }
                        } else {
                            // Base64 encoded values
                            if (line.attribute === "dn") {
                                //return;
                            }
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
                                        encodedvalue = val;
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
                                    namingAttr = attr;
                                    namingValue = val;
                                }
                            }

                            obj.id = generateUniqueId();
                            obj.attr = attr;
                            obj.val = val;
                            obj.encodedvalue = encodedvalue;
                            obj.namingAttr = namingAttribute;
                            obj.required = namingAttribute;
                            this.originalEntryRows.push(obj);
                        }
                        // Handle group members separately
                        if (attrLowerCase === "member" || attrLowerCase === "uniquemember") {
                            members.push(val);
                        }
                    } else {
                        // Value too large Label
                        const obj = {};
                        obj.id = generateUniqueId();
                        obj.attr = line.props.attr;
                        obj.val = line;
                        obj.encodedvalue = "";
                        obj.namingAttr = false;
                        obj.required = false;
                        attrLowerCase = line.props.attr.trim().toLowerCase();
                        this.originalEntryRows.push(obj);
                    }
                });

                // Mark the existing objectclass classes as selected
                this.state.localProps.allObjectclasses.map(oc => {
                    let selected = false;
                    let selectionDisabled = false;
                    for (const entryOC of objectclasses) {
                        if (entryOC.toLowerCase() === oc.name.toLowerCase()) {
                            // Mark required attributes with selected OC's
                            for (let row of this.originalEntryRows) {
                                if (oc.required.includes(row.attr) || row.attr === "dn") {
                                    row.required = true;
                                }
                            }
                            selected = true;
                            break;
                        }
                    }
                    if (oc.name === "top") {
                        // Can not remove objectclass=top
                        selectionDisabled = true;
                    }
                    ocArray.push(
                        {
                            cells: [
                                oc.name,
                                oc.required.join(', '),
                                oc.optional.join(', '),
                            ],
                            selected: selected,
                            disableSelection: selectionDisabled
                        });
                });
                const selectedObjectClasses = ocArray
                    .filter(item => item.selected);

                this.setState({
                    itemCountOc: ocArray.length,
                    rowsOc: ocArray,
                    pagedRowsOc: ocArray.slice(0, this.state.perPageOc),
                    selectedObjectClasses,
                    editableTableData: [...this.originalEntryRows],
                    objectclasses: objectclasses,
                    namingAttribute: namingAttr,
                    namingValue: namingValue,
                    origAttrs: JSON.parse(JSON.stringify(this.originalEntryRows)),
                    origOC: JSON.parse(JSON.stringify(selectedObjectClasses)),
                    isGroupOfNames: isGroupOfNames,
                    isGroupOfUniqueNames: isGroupOfUniqueNames,
                    groupMembers: members.sort(),
                    loading: false,
                }, () => {
                    this.updateAttributeTableRows();
                });
        });
    }

    componentDidMount () {
        getSingleValuedAttributes(this.props.editorLdapServer,
            (myAttrs) => {
                this.singleValuedAttributes = [...myAttrs];
        });
        this.setState({
            localProps:  {...this.props}
        }, () => { this.loadEntry() });
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

    onSelectOc = (event, isSelected, rowId) => {
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

        let attrsToRemove = [];
        if (!isSelected) {
            // Removing an objectclass, this will impact the entry as we might have to remove attributes
            let ocAttrs = allItems[index].cells[1].toLowerCase().replace(/\s/g, '').split(',');
            ocAttrs = ocAttrs.concat(allItems[index].cells[2].toLowerCase().replace(/\s/g, '').split(','));
            let currAttrs = [];
            for (const oc of selectedObjectClasses) {
                // Gather all the allowed attributes
                currAttrs = currAttrs.concat(oc.cells[1].toLowerCase().replace(/\s/g, '').split(','));
                currAttrs = currAttrs.concat(oc.cells[2].toLowerCase().replace(/\s/g, '').split(','));
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

    onSelectAttr = (event, isSelected, rowId) => {
        let newEditableData = this.state.editableTableData;
        let rows;

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
        const attrName = this.state.pagedRowsAttr[rowId].attributeName;
        const allItems = [...this.state.rowsAttr];
        const allAttrs = this.getAllAttrs();
        const index = allItems.findIndex(item => item.attributeName === attrName);
        allItems[index].isAttributeSelected = isSelected;
        let selectedAttributes = allAttrs
            .filter(item => item.isAttributeSelected)
            .map(attrObj => [attrObj.attributeName, attrObj.cells[1]]);


        if (isSelected) {
            // Add to selected attr
            selectedAttributes.push([allItems[index].attributeName, allItems[index].cells[1]]);
        } else {
            // Remove attr from selected list
            selectedAttributes = selectedAttributes.filter(row => (row[0] !== allItems[index].attributeName));
        }

        // Update the table rows as needed
        const rowAttr = rows[rowId].attributeName.toLowerCase();
        const found = this.state.editableTableData.filter(item => (item.attr.toLowerCase() === rowAttr));
        if (isSelected) {
            if (found.length === 0 && rowAttr !== 'objectclass') {
                let obj = {};
                obj.id = generateUniqueId();
                obj.attr = rows[rowId].attributeName;
                obj.val = "";
                obj.namingAttr = false;
                obj.required = false;
                newEditableData =  [...newEditableData, obj]
            }
        } else if (found.length > 0) {
            // Remove the row if present
            newEditableData = this.state.editableTableData.filter(item => (item.attr.toLowerCase() !== rowAttr));
        }

        let validMods = true;
        for (const row of newEditableData) {
            if (row.val === "") {
                validMods = false
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

    getAllAttrs = () => {
        const ocToProcess = [...this.state.selectedObjectClasses];
        let rowsAttr = [];
        let attrList = [];

        ocToProcess.map(oc => {
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
                    let obj = {};
                    obj.id = generateUniqueId();
                    obj.attr = attr;
                    obj.val = "";
                    obj.namingAttr = false;
                    obj.required = false;

                    this.setState(prevState => ({
                        editableTableData: [...prevState.editableTableData, obj],
                        validMods: false,
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
                        selected: selected,
                        cells: [attr, oc.cells[0]]
                    });
                }
            }

            // If we're editing a user then add nsRoleDN and nsAccountLock attributes to the possible list
            let personOC = false;
            for (const existingOC of this.state.selectedObjectClasses) {
                if (existingOC.cells[0].toLowerCase() === 'person' ||
                    existingOC.cells[0].toLowerCase() === 'nsperson') {
                    personOC = true;
                    break;
                }
            }
            const additionalAttrs = ['nsRoleDN', 'nsAccountLock'];
            for (const addAttr of additionalAttrs) {
                if ((personOC && !attrList.includes(addAttr))) {
                    let selected = false;
                    for (const existingRow of this.state.editableTableData) {
                        if (existingRow.attr.toLowerCase() === addAttr.toLowerCase()) {
                            selected = true;
                            break;
                        }
                    }

                    attrList.push(addAttr);
                    rowsAttr.push({
                        attributeName: addAttr,
                        isAttributeSelected: selected,
                        selected: selected,
                        cells: [addAttr, '']
                    });
                }
            }
        });

        return rowsAttr;
    }

    updateAttributeTableRows = () => {
        let rowsAttr = [];

        rowsAttr = this.getAllAttrs();

        // Update the rows where user can select the attributes.
        rowsAttr.sort((a, b) => (a.attributeName > b.attributeName) ? 1 : -1)
        this.setState({
            rowsAttr,
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
        let statementRows = [];
        let updateArray = [];
        let addArray = [];
        let removeArray = [];
        let ldifArray = [];
        let cleanLdifArray = [];
        let numOfChanges = 0;
        let isFilePath = false;

        // Check for row changes
        for (const originalRow of this.originalEntryRows) {
            // Check if the value has been changed by comparing
            // the unique IDs and the values.
            const matchingObj = this.state.editableTableData.find(elt => (elt.id === originalRow.id));

            // Check if original row was removed
            if (!matchingObj) {
                removeArray.push(originalRow);
                continue;
            }

            // Now check the value.
            const sameValue = matchingObj.val === originalRow.val;
            if (sameValue) {
                updateArray.push({ ...originalRow });
            } else {
                // Value has changed.
                const myNewObject = {
                    ...originalRow,
                    op: LDAP_OPERATIONS.replace,
                    new: matchingObj.val
                };

                if (matchingObj.encodedvalue) {
                    myNewObject.encodedvalue = matchingObj.encodedvalue;
                }
                updateArray.push(myNewObject);
            }
        }

        // Check for new rows
        for (const savedRow of this.state.editableTableData) {
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

        for (let datum of updateArray) {
            const myAttr = datum.attr;
            let myVal = datum.val;
            const isUserPwd = myAttr.toLowerCase() === "userpassword";
            isFilePath = false;

            if (myAttr === 'dn') { // Entry DN.
                ldifArray.push(`dn: ${myVal}`); // DN line.
                ldifArray.push('changetype: modify'); // To modify the entry.
                cleanLdifArray.push(`dn: ${myVal}`); // DN line.
                cleanLdifArray.push('changetype: modify'); // To modify the entry.
            }
            if (datum.op === undefined) { // Unchanged value.
                if (isUserPwd) {
                    myVal = "********";
                }
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
                    cleanLdifArray.push('-');
                }

                const sameAttrArray = this.originalEntryRows.filter(obj => obj.attr === myAttr);
                if ((typeof myVal === 'string' || myVal instanceof String) && (myVal.toLowerCase().startsWith("file:/"))) {
                    isFilePath = true;
                }
                const mySeparator = BINARY_ATTRIBUTES.includes(myAttr.toLowerCase())
                    ? (isFilePath ? ':<' : '::')
                    : ':';

                if (sameAttrArray.length > 1) {
                    // The attribute has multiple values.
                    // We need to delete the specific value and add the new one.
                    ldifArray.push(`delete: ${myAttr}`);
                    ldifArray.push(`${myAttr}: ${myVal}`);
                    ldifArray.push('-');
                    ldifArray.push(`add: ${myAttr}`);
                    cleanLdifArray.push(`delete: ${myAttr}`);
                    cleanLdifArray.push(`${myAttr}: ${myVal}`);
                    cleanLdifArray.push('-');
                    cleanLdifArray.push(`add: ${myAttr}`);
                } else {
                    // There is a single value for the attribute.
                    // A "replace" statement is enough.
                    ldifArray.push(`replace: ${myAttr}`);
                    cleanLdifArray.push(`replace: ${myAttr}`);
                }

                const valueToUse = datum.encodedvalue
                    ? datum.encodedvalue
                    : datum.new;
                // foldLine() will return the line as is ( in an array though )
                // if its length is smaller than 78.
                // Otherwise the line is broken into smaller ones ( 78 characters max per line ).
                const remainingData = foldLine(`${myAttr}${mySeparator} ${valueToUse}`);
                ldifArray.push(...remainingData);
                if (myAttr.toLowerCase().startsWith("userpassword")) {
                    cleanLdifArray.push("userpassword: ********");
                } else if (myAttr.toLowerCase().startsWith("jpegphoto") && mySeparator === '::') {
                    const myTruncatedValue = (<div>
                                                {"jpegphoto:: "}
                                                <Label icon={<InfoCircleIcon />} color="blue" >
                                                    Value is too large to display
                                                </Label>
                                            </div>);
                    cleanLdifArray.push(myTruncatedValue);
                } else {
                    cleanLdifArray.push(...remainingData);
                }
                numOfChanges++;
                if (isUserPwd) {
                    datum.new = "********";
                    myVal = "********";
                }
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
            let myVal = datum.val;
            const isUserPwd = myAttr.toLowerCase() === "userpassword";
            numOfChanges++;
            isFilePath = false;

            if ((typeof myVal === 'string' || myVal instanceof String) && (myVal.toLowerCase().startsWith("file:/"))) {
                isFilePath = true;
            }
            const mySeparator = BINARY_ATTRIBUTES.includes(myAttr.toLowerCase())
                ? (isFilePath ? ':<' : '::')
                : ':';

            // Update LDIF array
            if (ldifArray.length >= 4) { // There was already a first round of attribute replacement.
                ldifArray.push('-');
                cleanLdifArray.push('-');
            }
            ldifArray.push('add: ' + myAttr);
            cleanLdifArray.push('add: ' + myAttr);

            const valueToUse = datum.encodedvalue
                ? datum.encodedvalue
                : datum.val;

            const remainingData = foldLine(`${myAttr}${mySeparator} ${valueToUse}`);
            ldifArray.push(...remainingData);
            if (myAttr.toLowerCase().startsWith("userpassword")) {
                cleanLdifArray.push("userpassword: ********");
            } else if (myAttr.toLowerCase().startsWith("jpegphoto") && mySeparator === '::') {
                const myTruncatedValue = (<div>
                                            {"jpegphoto:: "}
                                            <Label icon={<InfoCircleIcon />} color="blue" >
                                                Value is too large to display
                                            </Label>
                                        </div>);
                cleanLdifArray.push(myTruncatedValue);
            } else {
                cleanLdifArray.push(...remainingData);
            }

            if (isUserPwd) {
                myVal = "********";
            }

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
            let myVal = datum.val;
            const isUserPwd = myAttr.toLowerCase() === "userpassword";
            isFilePath = false;
            // Update LDIF array
            if (ldifArray.length >= 4) { // There was already a first round of attribute replacement.
                ldifArray.push('-');
                cleanLdifArray.push('-');
            }

            if ((typeof myVal === 'string' || myVal instanceof String) && (myVal.toLowerCase().startsWith("file:/"))) {
                isFilePath = true;
            }
            const mySeparator = BINARY_ATTRIBUTES.includes(myAttr.toLowerCase())
                ? (isFilePath ? ':<' : '::')
                : ':';

            const valueToUse = datum.encodedvalue
                ? datum.encodedvalue
                : datum.val;
            ldifArray.push('delete: ' + myAttr);
            cleanLdifArray.push('delete: ' + myAttr);
            numOfChanges++;
            if (!isUserPwd) {
                const remainingData = foldLine(`${myAttr}${mySeparator} ${valueToUse}`);
                ldifArray.push(...remainingData);
                if (myAttr.toLowerCase().startsWith("userpassword")) {
                    cleanLdifArray.push("userpassword: ********");
                } else if (myAttr.toLowerCase().startsWith("jpegphoto") && mySeparator === '::') {
                    const myTruncatedValue = (<div>
                                                {"jpegphoto:: "}
                                                <Label icon={<InfoCircleIcon />} color="blue" >
                                                    Value is too large to display
                                                </Label>
                                            </div>);
                    cleanLdifArray.push(myTruncatedValue);
                } else {
                    cleanLdifArray.push(...remainingData);
                }
            } else {
                myVal = "********";
            }

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

        // Handle Objectclass changes
        const origOCs = this.state.origOC.map(oc => { return oc.cells[0].toLowerCase() });
        const newOCs = this.state.selectedObjectClasses.map(oc => { return oc.cells[0].toLowerCase() });
        for (const oldOC of origOCs) {
            if (newOCs.indexOf(oldOC) === -1) {
                if (ldifArray.length >= 4) {
                    ldifArray.push('-');
                    cleanLdifArray.push('-');
                }
                ldifArray.push('delete: objectClass');
                ldifArray.push('objectClass: ' + oldOC);
                cleanLdifArray.push('delete: objectClass');
                cleanLdifArray.push('objectClass: ' + oldOC);
                statementRows.push({
                    cells: [
                        { title: (<Label color="red">Delete</Label>) },
                        'objectClass',
                        {
                            title: (
                                <Label variant="outline" color="blue" isTruncated>
                                    {oldOC}
                                </Label>
                            )
                        }
                    ]
                });
                numOfChanges++;
            }
        }
        for (const newOC of newOCs) {
            if (origOCs.indexOf(newOC) === -1) {
                if (ldifArray.length >= 4) {
                    ldifArray.push('-');
                    cleanLdifArray.push('-');
                }
                ldifArray.push('add: objectClass');
                ldifArray.push('objectClass: ' + newOC);
                cleanLdifArray.push('add: objectClass');
                cleanLdifArray.push('objectClass: ' + newOC);
                statementRows.push({
                    cells: [
                        { title: (<Label color="orange">Add</Label>) },
                        'objectClass',
                        {
                            title: (
                                <Label variant="outline" color="blue" isTruncated>
                                    {newOC}
                                </Label>
                            )
                        }
                    ]
                });
                numOfChanges++;
            }
        }

        this.setState({
            statementRows,
            ldifArray,
            cleanLdifArray,
            numOfChanges: numOfChanges
        });
    }

    onOCDropDownToggle = isOpen => {
        this.setState({
            isOCDropDownOpen: isOpen
        });
    };

    onOCDropDownSelect = event => {
        this.setState((prevState, props) => {
            return { isOCDropDownOpen: !prevState.isOCDropDownOpen };
        });
    };

    buildOCDropdown= () => {
        const { isOCDropDownOpen, selectedObjectClasses } = this.state;
        const numSelected = selectedObjectClasses.length;
        let ocs = selectedObjectClasses.map((oc) => oc.cells[0]);
        ocs.sort();
        const items = ocs.map((oc) =>
            <DropdownItem key={oc}>{oc}</DropdownItem>
        );

        return (
            <Dropdown
                className="ds-dropdown-padding"
                onSelect={this.onOCDropDownSelect}
                position={DropdownPosition.left}
                toggle={
                    <BadgeToggle id="toggle-oc-select" onToggle={this.onOCDropDownToggle}>
                        {numSelected !== 0 ? <>{numSelected} selected </> : <>0 selected </>}
                    </BadgeToggle>
                }
                isOpen={isOCDropDownOpen}
                dropdownItems={items}
            />
        );
    }

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
        const numSelected = this.state.selectedAttributes.length;
        let attrs = selectedAttributes.map((attr) => attr[0]);
        attrs.sort();
        const items = attrs.map((attr) =>
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

    render () {
        const {
            loading, itemCountOc, pageOc, perPageOc, columnsOc, pagedRowsOc,
            itemCountAttr, pageAttr, perPageAttr, columnsAttr, pagedRowsAttr,
            commandOutput, namingAttribute, namingValue, stepIdReached,
            itemCount, pageAddUser, perPageAddUser, ldifArray, statementRows,
            resultVariant, editableTableData, numOfChanges,
            validMods, cleanLdifArray
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
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Select ObjectClasses
                        </Text>
                    </TextContent>
                    {this.buildOCDropdown()}
                </div>
                { loading &&
                    <div>
                        <Bullseye className="ds-margin-top-xlg" key="add-entry-bulleye" >
                            <Title headingLevel="h3" size="lg" key="loading-title">
                                Loading ...
                            </Title>
                        </Bullseye>
                        <Spinner className="ds-center" size="lg" key="loading-spinner" />
                    </div>
                }
                <div className={loading ? "ds-hidden" : ""}>
                    <Grid className="ds-margin-top-lg">
                        <GridItem span={5}>
                            <SearchInput
                                className="ds-font-size-md"
                                placeholder='Search Objectclasses'
                                value={this.state.searchOCValue}
                                onChange={this.onOCSearchChange}
                                onClear={(evt) => this.onOCSearchChange('', evt)}
                            />
                        </GridItem>
                        <GridItem span={7}>
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
                        </GridItem>
                    </Grid>
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
            </>
        );

        const attributeStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Select Attributes
                        </Text>
                    </TextContent>
                    {this.buildAttrDropdown()}
                </div>
                <Grid className="ds-margin-top-lg">
                    <GridItem span={5}>
                        <SearchInput
                            className="ds-font-size-md"
                            placeholder='Search Attributes'
                            value={this.state.searchValue}
                            onChange={this.onAttrSearchChange}
                            onClear={(evt) => this.onAttrSearchChange('', evt)}
                        />
                    </GridItem>
                    <GridItem span={7}>
                        <Pagination
                            itemCount={itemCountAttr}
                            page={pageAttr}
                            perPage={perPageAttr}
                            onSetPage={this.onSetPageAttr}
                            widgetId="pagination-step-attributes"
                            onPerPageSelect={this.onPerPageSelectAttr}
                            variant="top"
                            isCompact
                        />
                    </GridItem>
                </Grid>
                <Table
                    className="ds-margin-top"
                    cells={columnsAttr}
                    rows={pagedRowsAttr}
                    onSelect={this.onSelectAttr}
                    variant={TableVariant.compact}
                    aria-label="Pagination Attributes"
                    canSelectAll={false}
                >
                    <TableHeader />
                    <TableBody />
                </Table>

            </>
        );

        const myTitle = 'DN ( Distinguished Name )';
        const entryValuesStep = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Edit Attribute Values
                    </Text>
                </TextContent>
                <EditableTable
                    key={editableTableData}
                    wizardEntryDn={this.state.localProps.wizardEntryDn}
                    editableTableData={editableTableData}
                    quickUpdate
                    isAttributeSingleValued={this.isAttributeSingleValued}
                    isAttributeRequired={this.isAttributeRequired}
                    enableNextStep={this.enableNextStep}
                    saveCurrentRows={this.saveCurrentRows}
                    allObjectclasses={this.state.localProps.allObjectclasses}
                    disableNamingChange
                />
            </>
        );

        const ldifListItems = cleanLdifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={(typeof line === 'string' || line instanceof String) && line.startsWith('dn: ')}>
                {(typeof line === 'string' || line instanceof String)
                 ?
                   line.length < 1000
                   ?
                   line
                   :
                   (<div>
                       line.substring(0, 1000)
                       <Label icon={<InfoCircleIcon />} color="blue" >
                           Value is too large to display
                       </Label>
                   </div>)
                 :
                 line}
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
        });

        const entryReviewStep = (
            <div>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant={resultVariant}
                        isInline
                        title="Result for Entry Modification"
                    >
                        {commandOutput}
                        {this.state.adding &&
                            <div>
                                <Spinner className="ds-left-margin" size="md" />
                                &nbsp;&nbsp;Modifying entry ...
                            </div>
                        }
                    </Alert>
                </div>
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

        const editEntrySteps = [
            {
                id: 1,
                name: 'Select ObjectClasses',
                component: objectClassStep,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 6,
                enableNext: this.state.selectedObjectClasses.length > 0,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Select Attributes',
                component: attributeStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 6,
            },
            {
                id: 3,
                name: 'Edit Values',
                component: entryValuesStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 6,
                enableNext: validMods
            },
            {
                id: 4,
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
                canJumpTo: stepIdReached >= 4 && stepIdReached < 6,
                enableNext: numOfChanges > 0
            },
            {
                id: 5,
                name: 'LDIF Statements',
                component: ldifStatementsStep,
                nextButtonText: 'Modify Entry',
                canJumpTo: stepIdReached >= 5 && stepIdReached < 6
            },
            {
                id: 6,
                name: 'Review Result',
                component: entryReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached > 6,
                hideBackButton: true,
                enableNext: !this.state.modifying
            }
        ];

        const title = <>
            Entry DN: &nbsp;&nbsp;<strong>{this.state.localProps.wizardEntryDn}</strong>
        </>;

        let editPage =
            <Wizard
                isOpen={this.state.localProps.isWizardOpen}
                onClose={this.state.localProps.toggleOpenWizard}
                steps={editEntrySteps}
                title="Edit An LDAP Entry"
                description={title}
                onNext={this.onNext}
            />;

        if (!this.state.groupGenericEditor && (this.state.isGroupOfNames || this.state.isGroupOfUniqueNames)) {
            editPage =
                <EditGroup
                    key={this.state.groupMembers}
                    groupdn={this.state.localProps.wizardEntryDn}
                    members={this.state.groupMembers}
                    useGenericEditor={this.useGroupGenericEditor}
                    isGroupOfNames={this.state.isGroupOfNames}
                    isGroupOfUniqueNames={this.state.isGroupOfUniqueNames}
                    treeViewRootSuffixes={this.state.localProps.treeViewRootSuffixes}
                    editorLdapServer={this.state.localProps.editorLdapServer}
                    addNotification={this.state.localProps.addNotification}
                    openEditEntry={this.openEditEntry}
                    onReload={this.loadEntry}
                />;
        }

        return (
            <>
                {editPage}
            </>
        );
    }
}

export default EditLdapEntry;
