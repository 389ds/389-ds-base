import React from 'react';
import {
    Alert,
    BadgeToggle,
    Bullseye,
    Button,
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
    Table, TableHeader, TableBody, TableVariant,
    breakWord,
    headerCol,
} from '@patternfly/react-table';
import {
    b64DecodeUnicode,
    createLdapEntry,
    foldLine,
    generateUniqueId,
    getBaseLevelEntryAttributes,
    getRdnInfo,
    getSingleValuedAttributes,
} from '../../lib/utils.jsx';
import EditableTable from '../../lib/editableTable.jsx';
import {
    LDAP_OPERATIONS,
    BINARY_ATTRIBUTES,
    LDIF_MAX_CHAR_PER_LINE
} from '../../lib/constants.jsx';

class AddLdapEntry extends React.Component {
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
            namingRowID: -1,
            namingVal: '',
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
            rowsAttrOrig: [],
            rowsValues: [],
            pagedRowsOc: [],
            pagedRowsAttr: [],
            selectedObjectClasses: [],
            selectedAttributes: [],
            attrsToRemove: [],
            namingAttr: "",
            adding: true,
            searchOCValue: "",
            searchAttrValue: "",
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            // The function updateValuesTableRows() is called upon new seletion(s)
            // Make sure the values table is updated in case no selection was made.
            if (id === 3) {
                // Just call this function in order to make sure the values table is up-to-date
                // even after navigating back and forth.
                this.updateAttributeTableRows();
            } else if (id === 4) {
                // Remove attributes from removed objectclasses
                this.cleanUpEntry();
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
                            commandOutput: result.errorCode === 0 ? 'Successfully added entry!' : 'Failed to add entry, error: ' + result.errorCode ,
                            resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                            adding: false,
                        }, () => { this.props.onReload() });

                        const myDn = this.state.ldifArray[0].substring(4);
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
                searchOCValue: value,
                itemCountOc: ocRows.length,
            });
        }

        this.onAttrSearchChange = (value, event) => {
            let attrRows = [];
            let allAttrs = this.state.rowsAttrOrig;
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
        }
        // End constructor().
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
    }

    componentDidMount () {
        const ocArray = [];
        getSingleValuedAttributes(this.props.editorLdapServer,
            (myAttrs) => {
                this.singleValuedAttributes = [...myAttrs];
        });

        this.props.allObjectclasses.map(oc => {
            let selectionDisabled = false;
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
                    selected: false,
                    disableSelection: selectionDisabled
                });
        });

        this.setState({
            itemCountOc: ocArray.length,
            rowsOc: ocArray,
            pagedRowsOc: ocArray.slice(0, this.state.perPageOc),
            loading: false,
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
        const attrName = this.state.pagedRowsAttr[rowId].cells[0];
        let allItems = [...this.state.rowsAttrOrig];
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

    updateAttributeTableRows = () => {
        const ocToProcess = [...this.state.selectedObjectClasses];
        let rowsAttr = [];
        let attrList = [];
        let namingRowID = this.state.namingRowID;
        let namingAttr = this.state.namingAttr;
        let isNamingAttr = false;

        for (let oc of ocToProcess) {
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
                        isNamingAttr = true;
                    }

                    let obj = {};
                    obj.id = new_id;
                    obj.attr = attr;
                    obj.val = "";
                    obj.namingAttr = false;
                    obj.required = true;

                    this.setState(prevState => ({
                        editableTableData: [...prevState.editableTableData, obj],
                        validMods: false,
                        namingRowID: namingRowID,
                        namingAttr: namingAttr,
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
        }

        // Update the rows where user can select the attributes.
        rowsAttr.sort((a, b) => (a.attributeName > b.attributeName) ? 1 : -1)
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
        let valueData = [];

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
        ]

        // Hide userpassword value
        let cleanLdifArray = [...ldifArray];
        for (let idx in cleanLdifArray) {
            if (cleanLdifArray[idx].toLowerCase().startsWith("userpassword")) {
                cleanLdifArray[idx] = "userpassword: ********";
                break;
            }
        }

        this.setState({
            ldifArray,
            cleanLdifArray
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
        const numSelected = this.state.rowsOc.filter(item => item.selected).length;
        const items = this.state.selectedObjectClasses.map((oc) =>
            <DropdownItem key={oc.cells[0]}>{oc.cells[0]}</DropdownItem>
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
        const numSelected = selectedAttributes.length;
        const items = selectedAttributes.map((attr) =>
            <DropdownItem key={attr[0]}>{attr[0]}</DropdownItem>
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

    setNamingRowID = (namingRowID) => {
        let namingAttrVal = "";
        let namingAttr = "";
        let namingVal = "";
        let rows = this.state.editableTableData;

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


    render () {
        const {
            loading, itemCountOc, pageOc, perPageOc, columnsOc, pagedRowsOc,
            itemCountAttr, pageAttr, perPageAttr, columnsAttr, pagedRowsAttr,
            commandOutput, namingAttr, namingVal, stepIdReached,
            itemCount, pageAddUser, perPageAddUser, ldifArray, statementRows,
            resultVariant, editableTableData, numOfChanges, validMods,
            cleanLdifArray, selectedAttributes, selectedObjectClasses
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
                                Loading ObjectClasses ...
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
                                itemCount={itemCountOc}
                                page={pageOc}
                                perPage={perPageOc}
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
                            value={this.state.searchAttrValue}
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
                <Alert
                    variant={namingAttr === '' || namingVal === ''
                        ? 'warning'
                        : 'success'}
                    isInline
                    title={myTitle}
                >
                    <b>Entry DN:&nbsp;&nbsp;&nbsp;</b>{(namingAttr ? namingAttr : "??????")}={namingVal ? namingVal : "??????"},{this.props.wizardEntryDn}
                </Alert>
                <TextContent className="ds-margin-top">
                    <Text component={TextVariants.h3}>
                        Set Attribute Values
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
                                &nbsp;&nbsp;Adding entry ...
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
                enableNext: selectedObjectClasses.length > 0,
            },
            {
                id: 3,
                name: 'Select Attributes',
                component: attributeStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 6,
                enableNext: selectedAttributes.length > 0,
            },
            {
                id: 4,
                name: 'Edit Values',
                component: entryValuesStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 6,
                enableNext: validMods
            },
            {
                id: 5,
                name: 'LDIF Statements',
                component: ldifStatementsStep,
                nextButtonText: 'Create Entry',
                canJumpTo: stepIdReached >= 5 && stepIdReached < 6
            },
            {
                id: 6,
                name: 'Review Result',
                component: entryReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 6,
                hideBackButton: true,
                enableNext: !this.state.adding,
            }
        ];

        const title = <>
            Parent DN: &nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
        </>;

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                steps={addEntrySteps}
                title="Add An LDAP Entry"
                description={title}
                onNext={this.onNext}
            />
        );
    }
}

export default AddLdapEntry;
