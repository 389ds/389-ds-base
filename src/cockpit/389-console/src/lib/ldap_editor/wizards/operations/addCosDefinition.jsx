import React from 'react';
import {
    Alert,
    BadgeToggle,
    Bullseye,
    Button,
    Card,
    CardBody,
    CardTitle,
    Checkbox,
    Dropdown,
    DropdownItem,
    DropdownPosition,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Label,
    Modal,
    ModalVariant,
    Radio,
    SearchInput,
    SimpleList,
    SimpleListItem,
    Spinner,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    Tooltip,
    ValidatedOptions,
    Wizard,
} from '@patternfly/react-core';
import {
    headerCol,
    sortable,
} from '@patternfly/react-table';
import LdapNavigator from '../../lib/ldapNavigator.jsx';
import {
    generateUniqueId,
    getAttributesNameAndOid,
    createLdapEntry,
    runGenericSearch,
} from '../../lib/utils.jsx';
import {
    InfoCircleIcon
} from '@patternfly/react-icons';
import AddCosTemplate from './addCosTemplate.jsx';
import GenericPagination from '../../lib/genericPagination.jsx';


class AddCoS extends React.Component {
    constructor(props) {
        super(props);

        this.cosAttrsColumns = [
            { title: 'Name', transforms: [sortable] },
            { title: 'OID', transforms: [sortable] }
        ];
        this.singleValuedAttributes = ['cosTemplateDn', 'cosspecifier', 'cosIndirectSpecifier'];
        this.requiredAttributes = ['cn', 'cosattribute', 'cosspecifier', 'cosIndirectSpecifier'];

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
            namingVal: this.props.cosDefName,
            cosDescription: this.props.cosDefDesc,
            ldifArray: [],
            cleanLdifArray: [],
            savedRows: [],
            commandOutput: '',
            resultVariant: 'default',
            allAttributesSelected: false,
            attributeList: [],
            stepIdReached: this.props.stepReached,
            cosAttrs: [],
            // currentStepId: 1,
            itemCountAddCoS: 0,
            cosattrAttr: "",
            cosspecAttr: "",
            pageAddCoS: 1,
            perpageAddCoS: 10,
            columnsCoS: [
                { title: 'Attribute Name', cellTransforms: [headerCol()] },
                { title: 'From ObjectClass' }
            ],
            rowsCoS: [],
            pagedRowsCoS: [],
            selectedAttributes: ['cn'],
            isAttrDropDownOpen: false,
            cosAttrRows: [],
            tableModificationTime: 0,
            createTemplate: false,
            createTemplateEnd: false,
            createdDefiniton: '',
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
            adding: true,
            // reviewHelperText: 'LDIF data'
            editableTableData: [],
            rdn: "",
            rdnValue: "",
            parentDN: "",
            isTreeLoading: false,
            cosType: this.props.cosDefType,
            cosDNs: [],
            searching: false,
            searchPattern: "",
            showLDAPNavModal: false,
            showTemplateCreateModal: false,
            cosSearchBaseDn: "",
            cosParentBaseDn: "",
            cosAvailableOptions: [],
            cosTemplateDNSelected: this.props.createdTemplateDN,
            selectedItemProps: "",
            cossChosenOptions: [],
            isConfirmModalOpen: false,
        };

        this.handleConfirmModalToggle = () => {
            this.setState(({ isConfirmModalOpen }) => ({
              isConfirmModalOpen: !isConfirmModalOpen,
            }));
          };

        this.handleBaseDnSelection = (treeViewItem) => {
            this.setState({
                cosSearchBaseDn: treeViewItem.dn
            });
        }

        this.handleParentDnSelection = (treeViewItem) => {
            this.setState({
                cosParentBaseDn: treeViewItem.dn
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

        this.openTemplateCreateModal = () => {
            this.setState({
                showTemplateCreateModal: true
            });
        };

        this.closeTemplateCreateModal = () => {
            this.setState({
                showTemplateCreateModal: false
            });
        };

        this.handleCreateTemplate = () => {
            this.setState({
                createTemplate: true
            });
        };

        this.handleCreateTemplateEnd = () => {
            this.setState({
                createTemplateEnd: true
            }, () => { this.props.onReload() });
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            if (id === 7) {
                // Generate the LDIF data.
                this.updateValuesTableRows();
            } else if (id === 8) {
                // Create the LDAP entry.
                const myLdifArray = this.state.ldifArray;
                createLdapEntry(this.props.editorLdapServer,
                    myLdifArray,
                    (result) => {
                        const myDn = myLdifArray[0].substring(4);
                        this.setState({
                            commandOutput: result.errorCode === 0 ? 'CoS Definition successfully created!' : 'Failed to create cos: ' + result.errorCode,
                            resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                            adding: false,
                            createdDefiniton: myDn,
                        }, () => {
                            this.props.onReload();
                        });
                        // Update the wizard operation information.
                        const relativeDn = myLdifArray[5].replace(": ", "="); // cn val
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
            } else if ((id === 9) && (this.state.cosType === 'classic') && (this.state.resultVariant !== 'danger')) {
                this.setState({
                    isConfirmModalOpen: true
                });
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
                cosAvailableOptions: []
            }, () => { this.getEntries() });
        };

        this.handleSearchPattern = searchPattern => {
            this.setState({ searchPattern });
        };

        this.getEntries = () => {
            const baseDn = this.state.cosSearchBaseDn;
            const pattern = this.state.searchPattern;
            const filter = pattern === ''
                ? '(objectClass=costemplate)'
                : `(&(objectClass=costemplate)(cn=*${pattern}*))`;
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
                        <SimpleListItem key={dnLine}>
                            {dnLine}
                        </SimpleListItem>
                    );
                });

                this.setState({
                    cosAvailableOptions: newOptionsArray,
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
        }

        this.handleRadioChange = (_, event) => {
            this.setState({
                cosType: event.currentTarget.id,
            });
        }

        this.onSelectTemplate = (selectedItem, selectedItemProps) => {
            // Remove 'dn: ' from the string
            this.setState({ cosTemplateDNSelected: selectedItemProps.children.substring(4) });
        }

        this.handleChange = this.handleChange.bind(this);
    }

    componentDidMount() {
        // Populate the attribute list for choosing specifiers.
        if (this.state.cosAttrRows.length > 0) {
            // Data already fetched.
            return;
        }
        getAttributesNameAndOid(this.props.editorLdapServer, (resArray) => {
            const cosAttrRows = resArray.map(item => {
                return { cells: [item[0], item[1]], selected: false }
            });
            const attributeList = resArray.map(item => {
                return item[0];
            });
            const tableModificationTime = Date.now();
            this.setState({
                cosAttrRows,
                attributeList,
                tableModificationTime
            });
        });
        this.setState({
            parentDN: this.props.wizardEntryDn,
            cosSearchBaseDn: this.props.wizardEntryDn
        });
    }

    onSetpageAddCoS = (_event, pageNumber) => {
        this.setState({
            pageAddCoS: pageNumber,
            pagedRowsCoS: this.getAttributesToShow(pageNumber, this.state.perpageAddCoS)
        });
    };

    onPerPageSelectAddCoS = (_event, perPage) => {
        this.setState({
            pageAddCoS: 1,
            perpageAddCoS: perPage,
            pagedRowsCoS: this.getAttributesToShow(1, perPage)
        });
    };

    getAttributesToShow(page, perPage) {
        const start = (page - 1) * perPage;
        const end = page * perPage;
        const newRows = this.state.rowsCoS.slice(start, end);
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
            // Process the full table entries ( rowsCoS )
            rows = this.state.rowsCoS.map(oneRow => {
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
            if (this.state.cosType === 'pointer') {
                selectedAttributes.push('cosPointerDefinition');
                rows[1].selected = true;
                rows[1].isAttributeSelected = true;
            } else if (this.state.cosType === 'indirect') {
                selectedAttributes.push('cosIndirectDefinition');
                rows[1].selected = true;
                rows[1].isAttributeSelected = true;
            } else if (this.state.cosType === 'classic') {
                selectedAttributes.push('cosClassicDefinition');
                rows[1].selected = true;
                rows[1].isAttributeSelected = true;
            }
            this.setState({
                rowsCoS: rows,
                allAttributesSelected: isSelected,
                selectedAttributes
            },
                () => {
                    this.setState({
                        pagedRowsCoS: this.getAttributesToShow(this.state.pageAddCoS, this.state.perpageAddCoS)
                    });
                    this.updateValuesTableRows();
                });
        } else {
            // Quick hack until the code is upgraded to a version that supports "disableCheckbox"
            if (this.state.pagedRowsCoS[rowId].disableCheckbox === true) {
                return;
            } // End hack.

            // Process only the entries in the current page ( pagedRowsCoS )
            rows = [...this.state.pagedRowsCoS];
            rows[rowId].selected = isSelected;
            // Find the entry in the full array and set 'isAttributeSelected' accordingly
            // The property 'isAttributeSelected' is used to build the LDAP entry to add.
            // The row ID cannot be used since it changes with the pagination.
            const attrName = this.state.pagedRowsCoS[rowId].cells[0];
            let allItems = [...this.state.rowsCoS];
            const index = allItems.findIndex(item => item.cells[0] === attrName);
            allItems[index].isAttributeSelected = isSelected;
            const selectedAttributes = allItems
                .filter(item => item.isAttributeSelected)
                .map(selectedAttr => selectedAttr.cells[0]);

            this.setState({
                rowsCoS: allItems,
                pagedRowsCoS: rows,
                selectedAttributes
            }, () => this.updateValuesTableRows());
        }
    };

    updateValuesTableRows = (skipAttributeSelection) => {
        const newSelectedAttrs = ['cn'];
        let namingRowID = this.state.namingRowID;
        let namingAttrVal = this.state.namingAttrVal
        let editableTableData = [];
        let namingAttr = this.state.namingAttr;
        let namingVal = this.state.namingVal;
        let cosAttrs = this.state.cosAttrs;

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
        // Description
        if (this.state.cosDescription !== '') {
            const decs = this.state.cosDescription;
            editableTableData = editableTableData.filter((item) => ((item.attr.toLowerCase() !== 'description') || (item.val !== decs)));
            editableTableData.push({
                id: generateUniqueId(),
                attr: 'description',
                val: decs ? decs : '',
                required: true,
                namingAttr: false,
            });
        }
        // CoS Attribute
        if (cosAttrs.length > 0) {
            for (const i in cosAttrs) {
                const found = editableTableData.find(el => el.attr.toLowerCase() === 'cosattribute' && el.val.toLowerCase() === cosAttrs[i].name.toLowerCase());
                const attrVal = cosAttrs[i].name +
                                (cosAttrs[i].def ? " default" : "") +
                                (cosAttrs[i].override ? " override" : "") +
                                (cosAttrs[i].operational ? " operational" : "") +
                                (cosAttrs[i].opdefault ? " operational-default" : "") +
                                (cosAttrs[i].mergeschemes ? " merge-schemes" : "");

                if (found === undefined) {
                    // The new attribute was not in the list of saved attributes, add it.
                    editableTableData.push({
                        id: generateUniqueId(),
                        attr: 'cosattribute',
                        val: attrVal,
                        required: true,
                        namingAttr: false,
                    });
                }
            }
        }
        // Template DN
        if (this.state.cosType === 'pointer') {
            const dn_val = this.state.cosTemplateDNSelected
            editableTableData = editableTableData.filter((item) => ((item.attr.toLowerCase() !== 'costemplatedn') || (item.val !== dn_val)));
            editableTableData.push({
                id: generateUniqueId(),
                attr: 'cosTemplateDn',
                val: dn_val ? dn_val : '',
                required: false,
                namingAttr: false,
            });
        }
        // CoS Specifier
        let spec_attr;
        if (this.state.cosType !== 'pointer') {
            if (this.state.cosType === 'indirect') {
                spec_attr = 'cosindirectspecifier';
            } else {
                spec_attr = 'cosspecifier';
            }
            const spec_val = this.state.cosspecAttr;
            editableTableData = editableTableData.filter((item) => ((item.attr.toLowerCase() !== spec_attr) || (item.val !== spec_val)));
            editableTableData.push({
                id: generateUniqueId(),
                attr: spec_attr,
                val: spec_val ? spec_val : '',
                required: true,
                namingAttr: false,
            });
        }
        this.setState({
            savedRows: editableTableData
        });

        // Update the attributes to process.
        this.setState({
            editableTableData,
            rdn: editableTableData[0].attr,
            namingRowID,
            namingAttrVal,
            namingAttr,
            namingVal,
        }, () => { this.generateLdifData() });
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

        const dn_val = `${namingAttr}=${namingVal},${this.props.wizardEntryDn}`;
        rows = rows.filter((item) => (item.attr.toLowerCase() === 'costemplatedn'));
        rows.push({
            id: generateUniqueId(),
            attr: 'cosTemplateDn',
            val: dn_val,
            required: true,
            namingAttr: false,
        });

        this.setState({
            namingRowID,
            namingAttrVal,
            namingAttr,
            namingVal,
            editableTableData: rows
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
            'ObjectClass: cosSuperDefinition'];
        if (this.state.cosType === 'pointer') {
            objectClassData.push('ObjectClass: cosPointerDefinition');
        } else if (this.state.cosType === 'indirect') {
            objectClassData.push('ObjectClass: cosIndirectDefinition');
        } else if (this.state.cosType === 'classic') {
            objectClassData.push('ObjectClass: cosClassicDefinition');
        }

        let valueData = [];
        for (const item of this.state.savedRows) {
            const attrName = item.attr;
            valueData.push(`${attrName}: ${item.val}`);
            if (objectClassData.length === 4) { // There will a maximum of 4 ObjectClasses.
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

    handleChange(e) {
        const attr = e.target.id;
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [attr]: value,
        })
    }

    handleSelectedAttrs = (attrs) => {
        this.setState({
            cosAttrs: attrs.map(attr => {
                return {"name": attr,
                        "def": false,
                        "override": false,
                        "operational": false,
                        "opdefault": false,
                        "mergeschemes": false};
            })
        });
    };

    handleCheckboxChange(e, name) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        const param = e.target.id;
        if (param === "def") {
            this.setState(prevState => ({
                cosAttrs: prevState.cosAttrs.map(el => (el.name === name ? { ...el, def: value } : el))
            }));
        } else if (param === "override") {
            this.setState(prevState => ({
                cosAttrs: prevState.cosAttrs.map(el => (el.name === name ? { ...el, override: value } : el))
            }));
        } else if (param === "operational") {
            this.setState(prevState => ({
                cosAttrs: prevState.cosAttrs.map(el => (el.name === name ? { ...el, operational: value } : el))
            }));
        } else if (param === "opdefault") {
            this.setState(prevState => ({
                cosAttrs: prevState.cosAttrs.map(el => (el.name === name ? { ...el, opdefault: value } : el))
            }));
        } else if (param === "mergeschemes") {
            this.setState(prevState => ({
                cosAttrs: prevState.cosAttrs.map(el => (el.name === name ? { ...el, mergeschemes: value } : el))
            }));
        }
    }

    render() {
        const {
            cosDescription, ldifArray, resultVariant, cosAttrRows, stepIdReached, namingVal,
            cosSearchBaseDn, cosAvailableOptions, tableModificationTime, showLDAPNavModal,
            commandOutput, cosType, createTemplate, showTemplateCreateModal, cosTemplateDNSelected,
            createTemplateEnd, cosAttrs
        } = this.state;

        if (createTemplate) {
            return <AddCosTemplate
                isWizardOpen={this.props.isWizardOpen}
                toggleOpenWizard={this.props.toggleOpenWizard}
                wizardEntryDn={this.state.cosParentBaseDn}
                editorLdapServer={this.props.editorLdapServer}
                setWizardOperationInfo={this.props.setWizardOperationInfo}
                onReload={this.props.onReload}
                allObjectclasses={this.props.allObjectclasses}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                firstStep={this.props.firstStep}
                stepReached={2}
                definitionWizardEntryDn={this.props.wizardEntryDn}
                cosDefName={this.state.namingVal}
                cosDefDesc={this.state.cosDescription}
                cosDefType={this.state.cosType}
            />
        } else if (createTemplateEnd) {
            return <AddCosTemplate
                isWizardOpen={this.props.isWizardOpen}
                toggleOpenWizard={this.props.toggleOpenWizard}
                wizardEntryDn={this.state.createdDefiniton}
                editorLdapServer={this.props.editorLdapServer}
                setWizardOperationInfo={this.props.setWizardOperationInfo}
                onReload={this.props.onReload}
                allObjectclasses={this.props.allObjectclasses}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                firstStep={this.props.firstStep}
                stepReached={2}
                definitionWizardEntryDn=""
                cosDefCreateMoreTemplate
            />
        }

        const namingValAndTypeStep = (
            <div>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Select Name And CoS Type
                    </Text>
                </TextContent>
                <Form autoComplete="off">
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={2} className="ds-label">
                            CoS Name
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
                        <GridItem span={2} className="ds-label ds-margin-top-xlg">
                            Description
                        </GridItem>
                        <GridItem span={10} className="ds-margin-top-xlg">
                            <TextInput
                                value={cosDescription}
                                type="text"
                                id="cosDescription"
                                aria-describedby="cosDescription"
                                name="cosDescription"
                                onChange={(str, e) => {
                                    this.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>

                    <TextContent className="ds-margin-top-lg">
                        <Text component={TextVariants.h5}>
                            Choose CoS Type
                        </Text>
                    </TextContent>
                    <div className="ds-left-margin">
                        <Radio
                            name="cosType"
                            id="pointer"
                            value="pointer"
                            label="Pointer"
                            isChecked={this.state.cosType === 'pointer'}
                            onChange={this.handleRadioChange}
                            description="Identifies the template entry using the template DN only."
                        />
                        <Radio
                            name="cosType"
                            id="indirect"
                            value="indirect"
                            label="Indirect"
                            isChecked={this.state.cosType === 'indirect'}
                            onChange={this.handleRadioChange}
                            description="Identifies the template entry using the value of one of the target entry's attributes."
                            className="ds-margin-top"
                        />
                        <Radio
                            name="cosType"
                            id="classic"
                            value="classic"
                            label="Classic"
                            isChecked={this.state.cosType === 'classic'}
                            onChange={this.handleRadioChange}
                            description="Identifies the template entry using a combination of the template entry's base DN and the value of one of the target entry's attributes."
                            className="ds-margin-top"
                        />
                    </div>
                </Form>
            </div>
        );

        const selectCoSTemplate = (
            <React.Fragment>
                <Form autoComplete="off">
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Select CoS Template
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={12}>
                            <TextContent className="ds-margin-top">
                                <Text>
                                    The CoS template entry contains the value or values of the attributes generated
                                    by the CoS logic. The CoS template entry contains a general object class of cosTemplate
                                    The CoS template entries for a given CoS are stored in the directory tree along
                                    with the CoS definition.
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={9} className="ds-margin-top-xlg">
                            <TextContent>
                                <Text>
                                    Search Base:
                                    <Text
                                        className="ds-left-margin"
                                        component={TextVariants.a}
                                        onClick={this.openLDAPNavModal}
                                        href="#"
                                    >
                                        {cosSearchBaseDn}
                                    </Text>
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={3} className="ds-margin-top-xlg ds-right-align">
                            <Button
                                key="createTemplate"
                                variant="primary"
                                onClick={this.openTemplateCreateModal}
                            >
                                Create Template
                            </Button>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top">
                            <SearchInput
                                placeholder="Find CoS Template..."
                                value={this.state.searchPattern}
                                onChange={this.handleSearchPattern}
                                onSearch={this.handleSearchClick}
                                onClear={() => { this.handleSearchPattern('') }}
                            />
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top">
                            CoS Template Selected: <strong>&nbsp;&nbsp;{cosTemplateDNSelected}</strong>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            {(cosAvailableOptions.length !== 0) ?
                                <SimpleList onSelect={this.onSelectTemplate}>
                                    {cosAvailableOptions}
                                </SimpleList>
                                :
                                ""
                            }
                        </GridItem>

                        <Modal
                            variant={ModalVariant.medium}
                            title="Choose The New CoS Template Parent DN"
                            isOpen={showTemplateCreateModal}
                            onClose={this.closeTemplateCreateModal}
                            actions={[
                                <Button
                                    key="createTemplateModal"
                                    variant="primary"
                                    onClick={this.handleConfirmModalToggle}
                                    isDisabled={this.state.cosParentBaseDn === ""}
                                >
                                    Confirm
                                </Button>,
                                <Button
                                    key="cancelCreateTemplateModal"
                                    variant="primary"
                                    onClick={this.closeTemplateCreateModal}
                                >
                                    Close
                                </Button>
                            ]}
                        >
                            <Card isHoverable className="ds-indent ds-margin-bottom-md">
                                <CardBody>
                                    <LdapNavigator
                                        treeItems={[...this.props.treeViewRootSuffixes]}
                                        editorLdapServer={this.props.editorLdapServer}
                                        handleNodeOnClick={this.handleParentDnSelection}
                                        showTreeLoadingState={this.showTreeLoadingState}
                                    />
                                </CardBody>
                            </Card>
                        </Modal>
                        <Modal
                            variant={ModalVariant.small}
                            title="Leaving CoS Definiton Creation Wizard"
                            isOpen={this.state.isConfirmModalOpen}
                            onClose={this.handleConfirmModalToggle}
                            actions={[
                                <Button key="confirm" variant="primary" onClick={this.handleCreateTemplate}>
                                Confirm
                                </Button>,
                                <Button key="cancel" variant="link" onClick={this.handleConfirmModalToggle}>
                                Cancel
                                </Button>
                            ]}
                            >
                                You are about to leave CoS Definiton creation wizard. After you click 'Confirm',
                                you'll appear in CoS Template creation wizard and you won't able to return from there
                                until the process is finished.

                                Then you'll be able to use the created entry in the CoS definiton creation. It'll be
                                preselected for you automatically.
                        </Modal>
                        <Modal
                            variant={ModalVariant.medium}
                            title="Choose A Parent DN"
                            isOpen={showLDAPNavModal}
                            onClose={this.closeLDAPNavModal}
                            actions={[
                                <Button
                                    key="confirm"
                                    variant="primary"
                                    onClick={this.closeLDAPNavModal}
                                >
                                    Done
                                </Button>,
                            ]}
                        >
                            <Card isHoverable className="ds-indent ds-margin-bottom-md">
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

        const cosAttributesStep = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Choose CoS Attributes <Tooltip
                        position="bottom"
                        content={
                            <div>
                                The cosAttribute contains the name of the attribute for which to generate
                                a value for the CoS. There can be more than one cosAttribute value specified.
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>
                <GenericPagination
                    columns={this.cosAttrsColumns}
                    rows={cosAttrRows}
                    actions={null}
                    isSelectable={true}
                    canSelectAll={false}
                    enableSorting={true}
                    tableModificationTime={tableModificationTime}
                    handleSelectedAttrs={this.handleSelectedAttrs}
                    isSearchable
                />
                { cosAttrRows.length === 0 &&
                    // <div className="ds-margin-bottom-md" />
                    <Bullseye className="ds-margin-top-lg">
                        <center><Spinner size="lg"/></center>
                    </Bullseye>
                }
            </>
        );

        const cosConfigAttributesStep = (
            <>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>Configure CoS Attributes</Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={12}>
                        <TextContent className="ds-margin-top">
                            <Text>
                                The CoS attribute contains the name of another attribute
                                which is governed by the class of service. This attribute allows an override
                                qualifier after the attribute value which sets how the CoS handles existing
                                attribute values on entries when it generates attribute values.
                            </Text>
                            <Text>
                                Please, consult official documentation for more information.
                            </Text>
                            <Text>
                                If no qualifier is set, default is assumed.
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                <Grid key="header" className="ds-margin-top-lg">
                    <GridItem span={3}>
                        <TextContent>
                            <Text component={TextVariants.h4}>
                                Attribute Name
                            </Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={9}>
                        <TextContent>
                            <Text component={TextVariants.h4}>
                                Override Qualifiers
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                <hr />
                {cosAttrs.map(({ name, def, override, operational, opdefault, mergeschemes }, idx) => (
                    <Grid key={idx}>
                        <GridItem className="ds-label" span={3}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    {name}
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={3} title="Only returns a generated value if there is no corresponding attribute value stored with the entry.">
                            <Checkbox
                                id="def"
                                label="Default"
                                isChecked={def}
                                onChange={(checked, e) => {
                                    this.handleCheckboxChange(e, name, "def");
                                }}
                            />
                        </GridItem>
                        <GridItem span={3} title="Always returns the value generated by the CoS, even when there is a value stored with the entry.">
                            <Checkbox
                                id="override"
                                label="Override"
                                isChecked={override}
                                onChange={(checked, e) => {
                                    this.handleCheckboxChange(e, name, "override");
                                }}
                            />
                        </GridItem>
                        <GridItem span={3} title="Returns a generated attribute only if it is explicitly requested in the search. Operational attributes do not need to pass a schema check in order to be returned. When operational is used, it also overrides any existing attribute values.">
                            <Checkbox
                                id="operational"
                                label="Operational"
                                isChecked={operational}
                                onChange={(checked, e) => {
                                    this.handleCheckboxChange(e, name, "operational");
                                }}
                            />
                        </GridItem>
                        <GridItem span={3}/>
                        <GridItem span={3} title="Only returns a generated value if there is no corresponding attribute value stored with the entry and if it is explicitly requested in the search.">
                            <Checkbox
                                id="opdefault"
                                label="Operational-Default"
                                isChecked={opdefault}
                                onChange={(checked, e) => {
                                    this.handleCheckboxChange(e, name, "opdefault");
                                }}
                            />
                        </GridItem>
                        <GridItem span={6} title="Using the merge-schemes qualifier tells the CoS that it will, or can, generate multiple values for the managed attribute.">
                            <Checkbox
                                id="mergeschemes"
                                label="Merge-Schemes"
                                isChecked={mergeschemes}
                                onChange={(checked, e) => {
                                    this.handleCheckboxChange(e, name, "mergeschemes");
                                }}
                            />
                        </GridItem>
                        <hr />
                    </Grid>
                ))}
            </>
        );

        const cosSpecifierStep = (
            <>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>Select CoS Specifier</Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={12}>
                        <TextContent className="ds-margin-top">
                            <Text>
                                {this.state.cosType === 'indirect' ?
                                "Indirect type of CoS identifies the template entry based on the value of one of the target entry's attributes, as specified in the cosIndirectSpecifier attribute."
                                :
                                "Classic type of CoS identifies the template entry using both the template entry's DN (which you will assign later) and the value of one of the target entry's attributes (set in the cosSpecifier attribute)."}
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top-lg">
                    <GridItem span={3} className="ds-label">
                        <TextContent>
                            <Text component={TextVariants.h4}>
                                {this.state.cosType === 'indirect' ? 'Indirect ' : 'Classic '}Specifier
                            </Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={5} className="ds-left-margin">
                        <FormSelect id="cosspecAttr" value={this.state.cosspecAttr} onChange={(str, e) => { this.handleChange(e) }} aria-label="FormSelect Input">
                            <FormSelectOption value="" label="Select an attribute" isPlaceholder />
                            {this.state.attributeList.map((attr, index) => (
                                <FormSelectOption key={attr} value={attr} label={attr} />
                            ))}
                        </FormSelect>
                    </GridItem>
                </Grid>
            </>
        );

        const ldifListItems = ldifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const cosCreationStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title="LDIF Content for CoS Creation"
                />
                <Card isHoverable>
                    <CardBody>
                        {(ldifListItems.length > 0) &&
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
        const cosReviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title="Result for CoS Creation"
                >
                    {commandOutput}
                    {this.state.adding &&
                        <div>
                            <Spinner className="ds-left-margin" size="md" />
                            &nbsp;&nbsp;Adding CoS definition ...
                        </div>
                    }
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
                <Modal
                    variant={ModalVariant.small}
                    title="Create CoS Template"
                    isOpen={this.state.isConfirmModalOpen}
                    onClose={this.handleConfirmModalToggle}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={this.handleCreateTemplateEnd}>
                        Confirm
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleConfirmModalToggle}>
                        Cancel
                        </Button>
                    ]}
                    >
                    <Grid>
                        <GridItem span={12}>
                            <TextContent className="ds-margin-top">
                                <Text>
                                    You've chosen 'Classic' CoS type.
                                    cosTemplateDN attribute is set to {this.state.createdDefiniton}.
                                </Text>
                                <Text>
                                    Do you want to create a CoS template now?
                                </Text>
                                <Text>
                                    It will be added as a child to this entry: '{this.state.createdDefiniton}'
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                </Modal>
            </div>
        );

        const addCoSSteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 9,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Select Type',
                component: namingValAndTypeStep,
                enableNext: namingVal === '' ? false : true,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 9,
            },
            ...(cosType === 'pointer' ? [
                {
                    id: 3,
                    name: 'Select CoS Template',
                    component: selectCoSTemplate,
                    canJumpTo: stepIdReached >= 3 && stepIdReached < 9,
                    enableNext: this.state.cosTemplateDNSelected !== ''
                },
            ] : []),
            {
                id: 4,
                name: 'Select CoS Attributes',
                component: cosAttributesStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 9,
                enableNext: this.state.cosAttrs.length > 0
            },
            {
                id: 5,
                name: 'Configure CoS Attributes',
                component: cosConfigAttributesStep,
                canJumpTo: stepIdReached >= 5 && stepIdReached < 9,
                enableNext: this.state.cosAttrs.length > 0
            },
            ...(cosType !== 'pointer' ? [
                {
                    id: 6,
                    name: 'Select CoS Specifier',
                    component: cosSpecifierStep,
                    canJumpTo: stepIdReached >= 6 && stepIdReached < 9,
                    enableNext: this.state.cosspecAttr !== ''
                },
            ] : []),
            {
                id: 7,
                name: 'Create CoS',
                component: cosCreationStep,
                nextButtonText: 'Create',
                canJumpTo: stepIdReached >= 7 && stepIdReached < 9,
            },
            {
                id: 8,
                name: 'Review Result',
                component: cosReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 8 && stepIdReached < 9,
                hideBackButton: true,
                enableNext: !this.state.adding
            },
            ...((this.state.cosType === 'classic') && (resultVariant !== 'danger') ? [
                {
                    id: 9,
                    name: 'Create Templates',
                    component: cosReviewStep,
                    nextButtonText: 'Finish',
                    canJumpTo: stepIdReached > 9,
                    hideBackButton: true,
                    enableNext: !this.state.adding
                }
            ] : []),
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
                title="Add A CoS Definition"
                description={title}
                steps={addCoSSteps}
            />
        );
    }
}

export default AddCoS;
