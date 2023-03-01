import React from 'react';
import {
    Alert,
    Button,
    FileUpload,
    InputGroup,
    Label,
    Popover,
    Modal,
    ModalVariant,
    Radio,
    TextInput,
    ValidatedOptions,
} from '@patternfly/react-core';
import {
    EditableTextCell,
    Table, TableHeader, TableBody, TableVariant,
    applyCellEdits,
    breakWord,
    cancelCellEdits,
    validateCellEdits,
} from '@patternfly/react-table';
import {
    EyeIcon,
    EyeSlashIcon,
    InfoCircleIcon,
} from '@patternfly/react-icons';
import {
    BINARY_ATTRIBUTES,
    WEB_SOCKET_MAX_PAYLOAD,
} from './constants.jsx';
import {
    generateUniqueId,
    getRdnInfo,
} from './utils.jsx';
import { file_is_path } from "../../tools.jsx";
import PropTypes from "prop-types";

class EditableTable extends React.Component {
    constructor (props) {
        super(props);

        this.attributeValidationRules = [
            {
                name: 'required',
                validator: value => value.trim() !== '',
                errorText: 'This field is required'
            }
        ];

        this.columns = [
            { title: 'Attribute' },
            { title: 'Value', cellTransforms: [breakWord] }
        ];

        this.state = {
            tableRows: [],
            namingRowID: 0,
            isPasswordField: false,
            showPassword: false,
            pwdValue: "",
            pwdRowIndex: -1,
            fileValue: "",
            fileName: '',
            encodedValueIsEmpty: true,
            strPathValueIsEmpty: true,
            showFileUrl: false,
            isDnModalOpen: false,
            attrIsJpegPhoto: false,
            currentRowIndex: 0,
            binaryAttributeRadio: 'Upload',
            binaryAttributeFilePath: '',
            isFileTooLarge: false
        };

        this.handleDnModalToggle = () => {
            this.setState(({ isDnModalOpen }) => ({
                isDnModalOpen: !isDnModalOpen
            }));
        };

        this.handlePasswordToggle = event => {
            this.setState(({ isPasswordField }) => ({
                isPasswordField: !isPasswordField
            }));
        };

        this.handlePwdChange = (str, e) => {
            this.setState({
                pwdValue: str
            });
        };

        this.handlePwdSave = () => {
            const newRows = [...this.state.tableRows];
            if (this.state.pwdRowIndex !== -1) {
                newRows[this.state.pwdRowIndex].cells[1].props.value = this.state.pwdValue;
                this.setState({
                    tableRows: newRows,
                    isPasswordField: false
                }, () => {
                    const foundEmptyValue = newRows.find(el => el.cells[1].props.value === '');
                    if (foundEmptyValue === undefined) {
                        this.props.enableNextStep(true);
                    } else {
                        this.props.enableNextStep(false);
                    }
                    const rowDataToSave = this.buildRowDataToSave();
                    this.props.saveCurrentRows(rowDataToSave, this.state.namingRowID);
                });
            }
        };

        this.showOrHidePassword = () => {
            this.setState(({ showPassword }) => ({
                showPassword: !showPassword
            }));
        };

        this.applyCustomPath = (filePath) => {
            const encodedValueIsEmpty = true;
            const strPathValueIsEmpty = true;

            this.setState({
                encodedValueIsEmpty,
                strPathValueIsEmpty,
            });

            let encodedValue = '';
            let myDecodedValue = `file://${filePath}`;

            const newRows = [...this.state.tableRows];
            newRows[this.state.currentRowIndex].cells[1].props.value = myDecodedValue;
            newRows[this.state.currentRowIndex].cells[1].props.encodedvalue = encodedValue;

            this.setState(({ showFileUri }) => ({
                encodedValueIsEmpty: true,
                strPathValueIsEmpty: false,
                showFileUri: !showFileUri,
                tableRows: newRows
            }));
            const foundEmptyValue = newRows.find(el => el.cells[1].props.value === '');
            if (foundEmptyValue === undefined) {
                this.props.enableNextStep(true);
            } else {
                this.props.enableNextStep(false);
            }
            const rowDataToSave = this.buildRowDataToSave();
            this.props.saveCurrentRows(rowDataToSave, this.state.namingRowID);
        };

        this.handleToggleUri = () => {
            if (!this.state.strPathValueIsEmpty) {
                this.applyCustomPath(this.state.binaryAttributeFilePath);
            } else {
                this.setState(({ showFileUri }) => ({
                    showFileUri: !showFileUri,
                }));
            }
        };

        this.handleClear = () => {
            this.setState({
                fileName: ""
            });
        };

        this.handleFileChange = (e, file) => {
            let encodedValue;
            const encodedValueIsEmpty = true;
            const strPathValueIsEmpty = true;
            const isFileTooLarge = false;

            this.setState({
                fileName: file.name,
                encodedValueIsEmpty,
                strPathValueIsEmpty,
                isFileTooLarge
            });
            console.debug('handleFileChange - file: ', file);

            if (file.size === undefined) { // The "Clear" button was pressed.
                return;
            }

            if (file.size === 0) { // An empty file was selected.
                console.log('An empty file was selected. Nothing to do.');
                return;
            }

            // TODO: Remove this limit?
            // See https://github.com/cockpit-project/cockpit/issues/12338
            // for ideas.
            //
            // https://github.com/cockpit-project/cockpit/blob/dee6324d037f3b8961d1b38960b4226c7e473abf/src/websocket/websocketconnection.c#L154
            //
            if (file.size > WEB_SOCKET_MAX_PAYLOAD) {
                console.log('handleFileChange - File too large!');
                this.setState({ isFileTooLarge: true });
                return;
            }
            // data:application/x-x509-ca-cert;base64
            // data:image/png;base64,
            // base64encode(fileName, val => { console.log(val); })
            // https://stackoverflow.com/questions/36280818/how-to-convert-file-to-base64-in-javascript

            const reader = new FileReader();
            reader.readAsDataURL(file);
            reader.onload = () => {
                const pattern = ';base64,';
                const pos = reader.result.indexOf(pattern);
                const toDel = reader.result.substring(0, pos + pattern.length);
                const isAnImage = file.type.startsWith('image/');

                // Check the file type.
                encodedValue = this.state.attrIsJpegPhoto
                ? isAnImage
                    ? reader.result.replace(toDel, '')
                    : ""
                // The attribute is a certificate.
                : (file.type === 'application/x-x509-ca-cert') ||
                (file.type === 'application/x-x509-user-cert') ||
                (file.type === 'application/pkix-cert')
                    ? reader.result.replace(toDel, '')
                    : "";

                if (encodedValue === "") {
                    console.log('handleFileChange - encodedValue is null. Nothing to do.');
                    return;
                }

                // Decode the binary value.
                let myDecodedValue = "";
                if (isAnImage) {
                    if (this.state.attrIsJpegPhoto) {
                        myDecodedValue = (<img
                            src={`data:image/png;base64,${encodedValue}`}
                            alt=""
                            style={{ width: '48px' }} // height will adjust automatically.
                            />);
                    } else {

                    }
                } else {
                    // TODO ==> Decode the certificate
                    // IMPORTANT! ==> Enable the "Confirm" button once the cert decoding is completed.
                    // myDecodedValue = ...
                    myDecodedValue = (<div>
                        <Label icon={<InfoCircleIcon />} color="blue" >
                            Value is too large to display
                        </Label>
                        </div>);
                }

                // console.log(reader.result.substring(0, 100));
                console.log(`handleFileChange - encodedValue.substring(0, 100) = ${encodedValue.substring(0, 100)}`);
                const newRows = [...this.state.tableRows];
                newRows[this.state.currentRowIndex].cells[1].props.value = myDecodedValue;
                // Store the encoded value to use it to create the LDIF statements!
                newRows[this.state.currentRowIndex].cells[1].props.encodedvalue = encodedValue;

                this.setState({
                    encodedValueIsEmpty: false,
                    strPathValueIsEmpty: true,
                    tableRows: newRows
                });
                const foundEmptyValue = newRows.find(el => el.cells[1].props.value === '');
                if (foundEmptyValue === undefined) {
                    this.props.enableNextStep(true);
                } else {
                    this.props.enableNextStep(false);
                }
                const rowDataToSave = this.buildRowDataToSave();
                this.props.saveCurrentRows(rowDataToSave, this.state.namingRowID);
            };
            reader.onerror = (error) => {
                console.log(`handleFileChange - Failed to encode the file : ${file.name}`, error);
            };
        };

        this.handleRadioOnChange = (_, event) => {
            this.setState({ binaryAttributeRadio: event.currentTarget.value });
        };

        this.handleBinaryAttributeInput = (str, e) => {
            const invalidPath = !file_is_path(str);

            this.setState({
                binaryAttributeFilePath: str,
                encodedValueIsEmpty: true,
                // If we say strPathValueIsEmpty is true then the 'Confirm' button will be disabled
                // Hense if we say strPathValueIsEmpty = invalidPath = true then the button is disabled
                strPathValueIsEmpty: invalidPath
            });
        };
    } // End constructor().

    componentDidMount() {
        this.createTableRows();
        const foundEmptyValue = this.props.editableTableData.find(el => el.val === '');
        this.props.enableNextStep(foundEmptyValue === undefined);
    }

    handleTextInputChange = (newValue, evt, rowIndex, cellIndex) => {
        const newRows = [...this.state.tableRows];
        newRows[rowIndex].cells[cellIndex].props.editableValue = newValue;
        this.setState({
            tableRows: newRows
        });
    };

    updateEditableRows = (evt, type, isEditable, rowIndex, validationErrors) => {
        // Do not allow to update the DN in the quick update mode.
        // Show a modal dialog to suggest to use the Renaming Wizard.
        if ((this.props.quickUpdate) && (type === 'edit')) {
            // DN row.
            if (rowIndex === 0) {
                this.handleDnModalToggle();
                return;
            }
            // RDN row.
            const attrCell = this.state.tableRows[rowIndex].cells[0];
            const valCell = this.state.tableRows[rowIndex].cells[1];
            const myAttr = (attrCell.props.value).toLowerCase();
            const myVal = valCell.props.value;
            const rdnInfo = getRdnInfo(this.props.wizardEntryDn);

            if ((myAttr === rdnInfo.rdnAttr.toLowerCase()) &&
            (myVal === rdnInfo.rdnVal)) {
                this.handleDnModalToggle();
                return;
            }
        }

        // Handle the edition of special attributes.
        if (type === 'edit') {
            const myCellData = this.state.tableRows[rowIndex].cells[0];
            const myAttr = (myCellData.props.value).toLowerCase();
            if (myAttr === 'userpassword') {
                this.setState({
                    isPasswordField: true,
                    pwdRowIndex: rowIndex
                 });
                return;
            } else if (BINARY_ATTRIBUTES.includes(myAttr)) {
                this.setState({
                    showFileUri: true,
                    attrIsJpegPhoto: myAttr === 'jpegphoto',
                    currentRowIndex: rowIndex
                });
                return;
            }
        }

        const newRows = [...this.state.tableRows];
        if (validationErrors && Object.keys(validationErrors).length) {
            newRows[rowIndex] = validateCellEdits(newRows[rowIndex], type, validationErrors);
            this.setState({ tableRows: newRows });
            return;
        }

        if (type === 'cancel') {
            newRows[rowIndex] = cancelCellEdits(newRows[rowIndex]);
            this.setState({
                tableRows: newRows
            });
            // if (typeof this.props.enableNextStep === 'function') {
            // Check if there is any empty value.
            const foundEmptyValue = newRows.find(el => el.cells[1].props.value === '');
            this.props.enableNextStep(foundEmptyValue === undefined);
            // }
            return;
        }

        newRows[rowIndex] = applyCellEdits(newRows[rowIndex], type);
        this.setState({ tableRows: newRows },
            () => {
                // Check if there is any empty value.
                const foundEmptyValue = newRows.find(el => el.cells[1].props.value === '');
                if (foundEmptyValue === undefined) {
                    const noEmptyValueCheck = type === 'save'; // Disable the next button on edit ( type='edit' ).
                    const namingAttrSet = this.state.namingRowID !== -1;
                    const enableNext = noEmptyValueCheck && namingAttrSet;
                    this.props.enableNextStep(enableNext);
                } else {
                    this.props.enableNextStep(false);
                }
                if (type === 'save') {
                    const rowDataToSave = this.buildRowDataToSave();
                    this.props.saveCurrentRows(rowDataToSave, this.state.namingRowID);
                }
            });
    };

    // Returns an array of row data to store in the parent component.
    buildRowDataToSave = () => {
        return this.state.tableRows.map(datum => {
            const obj = {
                id: datum.cells[0].props.name,
                attr: datum.cells[0].props.value,
                val: datum.cells[1].props.value,
                namingAttr: datum.namingAttr,
                required: datum.required,
                objectClass: datum.objectClass,
                rowEditValidationRules: [...datum.rowEditValidationRules],
            }
            // Save the encoded (base64) value if it's present.
            if (datum.cells[1].props.encodedvalue) {
                obj.encodedvalue = datum.cells[1].props.encodedvalue;
            }

            return obj;
        });
    };

    // Check if there is only one occurrence of an attribute
    // so we prevent its removal in case it is required.
    hasSingleOccurrence = (attributeName) => {
        // console.log(`attributeName = ${attributeName}`);
        let nb = 0;
        for (const datum of this.state.tableRows) {
            const attr = datum.cells[0].props.value;
            if (attr === attributeName) {
                nb++;
                if (nb > 1) {
                    return false;
                }
            }
        }
        return true;
    };

    generateSingleRow = (myData) => {
        const attrName = myData.attr;
        const attrId = myData.id;

        if (attrName.toLowerCase() === 'userpassword') {
            return ({
                objectClass: myData.obj,
                required: myData.required,
                namingAttr: myData.namingAttr,
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
                            </React.Fragment>
                        ),
                        props: {
                            value: attrName,
                            name: attrId,
                        }
                    },
                    {
                        title: (value, rowIndex, cellIndex, props) => (
                            <React.Fragment>
                                <EditableTextCell
                                    // isDisabled={myData.isDisabled}
                                    value={value !== "" ? "********" : <Label color="red" icon={<InfoCircleIcon />}> Empty value! </Label>}
                                    rowIndex={rowIndex}
                                    cellIndex={cellIndex}
                                    props={props}
                                    handleTextInputChange={this.handleTextInputChange}
                                    inputAriaLabel={'_' + value} // To avoid empty property when value is empty.
                                />
                            </React.Fragment>
                        ),
                        props: {
                            value: myData.val,
                            encodedvalue: myData.encodedvalue,
                            name: `${attrId}_val`
                        }
                    }
                ]
            });
        } else {
            return ({
                objectClass: myData.obj,
                required: myData.required,
                namingAttr: myData.namingAttr,
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
                                {
                                    (attrId === this.state.namingRowID || myData.namingAttr) &&
                                    <Popover
                                        headerContent={<div>Naming Attribute</div>}
                                        bodyContent={
                                            <div>
                                                This attribute and value are part of
                                                the entry's DN and can not be changed
                                                except by doing a Rename (modrdn) operation
                                                on the entry.
                                            </div>
                                        }
                                    >
                                        <a href="#" className="ds-font-size-sm">Naming Attribute</a>
                                    </Popover>
                                }
                            </React.Fragment>
                        ),
                        props: {
                            value: attrName,
                            name: attrId,
                        }
                    },
                    {
                        title: (value, rowIndex, cellIndex, props) => (
                            <React.Fragment>
                                <EditableTextCell
                                    // isDisabled={myData.isDisabled}
                                    value={value === "" ? <Label color="red" icon={<InfoCircleIcon />}> Empty value! </Label> : value}
                                    rowIndex={rowIndex}
                                    cellIndex={cellIndex}
                                    props={props}
                                    handleTextInputChange={this.handleTextInputChange}
                                    inputAriaLabel={'_' + value} // To avoid empty property when value is empty.
                                />
                            </React.Fragment>
                        ),
                        props: {
                            value: myData.val,
                            encodedvalue: myData.encodedvalue,
                            name: `${attrId}_val`
                        }
                    }
                ]
            });
        }
    };

    createTableRows = () => {
        // Update the rows where user can set the values.
        this.setState({
            namingRowID: this.props.namingRowID
        }, () => {
            const tableRows = this.props.editableTableData.map(attrData => {
                return this.generateSingleRow(attrData);
            });
            this.setState({
                tableRows
            });
        });
    };

    actionResolver = (rowData, { rowIndex }) => {
        const myAttr = rowData.cells[0].props.value;
        const myName = rowData.cells[0].props.name;
        if (myAttr === "dn") {
            // There should not be an actions for the DN
            return [];
        }

        const namingAction = myName === this.state.namingRowID || this.props.disableNamingChange
            ? []
            : (this.props.namingAttr && myAttr !== this.props.namingAttr) || this.props.namingAttr === ""
                ? [{
                    title: 'Set as Naming Attribute',
                    onClick: () => {
                        const foundEmptyValue = this.state.tableRows.find(el => el.cells[1].props.value === '');
                        this.props.enableNextStep(foundEmptyValue === undefined);

                        this.setState({
                            namingRowID: myName,
                            rdnAttr: myAttr
                        },
                        () => {
                            this.props.setNamingRowID(myName);
                        });
                    }
                }]
                : []

        const duplicationAction =
            this.props.isAttributeSingleValued(myAttr)
                ? []
                : [{
                    title: 'Add Another Value',
                    onClick: (event, rowId, rowData) => {
                        const myData = {
                            id: generateUniqueId(),
                            attr: myAttr,
                            val: ''
                        }
                        const tableRows = [...this.state.tableRows];
                        const newItem = this.generateSingleRow(myData);

                        // Insert the duplicate item right below the original.
                        tableRows.splice(rowIndex + 1, 0, newItem);

                        this.setState({ tableRows },
                            () => {
                                const rowDataToSave = this.buildRowDataToSave();
                                this.props.saveCurrentRows(rowDataToSave, this.state.namingRowID);
                                // Disable the next step because a duplicated row has no initial value.
                                this.props.enableNextStep(false);
                                    });
                                }
                    }];

        const removalAction = this.state.tableRows.length === 1 || rowData.namingAttr ||
            ((this.props.isAttributeRequired(myAttr) || rowData.required) && this.hasSingleOccurrence(myAttr))
            ? []
            : [
                {
                    title: 'Remove this Row',
                    onClick: (event, rowId) => {
                        const tableRows = this.state.tableRows.filter((aRow) => aRow.cells[0].props.name !== myName);
                        this.setState({ tableRows },
                            () => {
                                const rowDataToSave = this.buildRowDataToSave();
                                const foundEmptyValue = tableRows.find(el => el.cells[1].props.value === '');
                                this.props.enableNextStep(foundEmptyValue === undefined);

                                let namingRowID = this.state.namingRowID;
                                if (rowId === namingRowID) {
                                    namingRowID = -1;
                                    this.props.setNamingRowID(-1);
                                    this.props.enableNextStep(false);
                                }

                                this.setState({ namingRowID });
                                this.props.saveCurrentRows(rowDataToSave, namingRowID);
                            });
                    }
                }
            ];

        const nam = namingAction.length > 0;
        const dup = duplicationAction.length > 0;
        const rem = removalAction.length > 0;
        const firstSeparator = nam && (dup || rem) ? [{ isSeparator: true }] : [];
        const secondSeparator = dup && rem ? [{ isSeparator: true }] : [];

        if (this.props.quickUpdate) {
            // Allow some actions in quick update.
            let actions = [];
            if (dup) {
                actions = duplicationAction;
                if (rem) {
                    actions.push({ isSeparator: true });
                }
            }
            if (rem) {
                actions.push(removalAction[0]);
            } else {
                actions.push({
                    title: 'Required Attribute',
                    isDisabled: true
                });
            }
            return actions;
        } else {
            // Full option list
            return [
                ...namingAction,
                ...firstSeparator,
                ...duplicationAction,
                ...secondSeparator,
                ...removalAction
            ];
        }
    }

    render () {
        const {
            tableRows,
            isPasswordField, showPassword,
            showFileUri, isDnModalOpen,
            fileName, fileValue,
            attrIsJpegPhoto,
            encodedValueIsEmpty,
            strPathValueIsEmpty,
            binaryAttributeRadio,
            binaryAttributeFilePath,
            isFileTooLarge
        } = this.state;

        /* const binaryAttrMsg = showFileUri
          ? attrIsJpegPhoto
            ? 'This is a binary attribute. Use the file chooser to select ' +
              'the new picture from the local machine.' // \n' +
              // 'The allowed extensions are "JPEG" and "JPG" ( case-insensitive ).'
            : 'This is a binary attribute. Use the file chooser to select ' +
              'the new certificate from the local machine. \n' +
              'The certificate must be stored in the Distinguished Encoding Rules (DER) format' // . \n' +
              // 'The allowed extension is "DER" ( case-insensitive ).'
          : null; */

        const uploadSelected = showFileUri
            ? binaryAttributeRadio === 'Upload'
            : null;

        const fileUriModalTitle = uploadSelected
            ? `Upload File ${attrIsJpegPhoto ? '(Photo)' : '(Certificate)'}`
            : `Type File Path ${attrIsJpegPhoto ? '(Photo)' : '(Certificate)'}`;

        return (
            <React.Fragment>
                { isDnModalOpen &&
                    <Modal
                        id="modal-dn-id"
                        variant={ModalVariant.small}
                        titleIconVariant="info"
                        title="DN Renaming"
                        isOpen={this.state.isDnModalOpen}
                        onClose={this.handleDnModalToggle}
                        actions={[
                            <Button key="close" variant="primary" onClick={this.handleDnModalToggle}>
                                Close
                            </Button>
                        ]}
                    >
                        You cannot modify the RDN ( Relative Distinguished Name ) in the <strong>Quick Update</strong> mode.
                        <br />
                        If you want to modify the RDN, please use the <strong>Rename</strong> action option
                    </Modal>
                }

                {isPasswordField &&
                    <Modal
                        id="modal-password-id"
                        variant={ModalVariant.medium}
                        title="Set Password"
                        isOpen={isPasswordField}
                        onClose={this.handlePasswordToggle}
                        actions={[
                            <Button key="confirm" variant="primary" name="confirm"
                                onClick={this.handlePwdSave}
                            >
                                Confirm
                            </Button>,
                            <Button key="cancel" variant="link"
                                onClick={this.handlePasswordToggle}
                            >
                                Cancel
                            </Button>
                        ]}
                    >
                        <InputGroup>
                            <TextInput
                                name="passwordField"
                                id="passwordField"
                                type={showPassword ? 'text' : 'password'}
                                onChange={this.handlePwdChange}
                                aria-label="password field"
                            />
                            <Button
                                variant="control"
                                aria-label="password field icon"
                                onClick={this.showOrHidePassword}
                                icon={showPassword ? <EyeSlashIcon /> : <EyeIcon />}
                            />
                        </InputGroup>
                    </Modal>
                }

                { showFileUri &&
                    <Modal
                        variant={ModalVariant.medium}
                        title={fileUriModalTitle}
                        isOpen={showFileUri}
                        onClose={this.handleToggleUri}
                        actions={[
                            <Button
                                key="confirm"
                                variant="primary"
                                isDisabled={encodedValueIsEmpty && strPathValueIsEmpty}
                                onClick={this.handleToggleUri}
                            >
                                Confirm
                            </Button>,
                            <Button key="cancel" variant="link" onClick={this.handleToggleUri}>
                                Cancel
                            </Button>
                        ]}
                    >
                        <Radio
                            value="Upload"
                            className="ds-margin-top"
                            isChecked={uploadSelected}
                            onChange={this.handleRadioOnChange}
                            label="Upload a file local to the browser."
                            description="Select a file from the machine on which the browser was launched."
                            name="radio-binary-attribute"
                            id="radio-upload-binary-attribute"
                        />
                        <Radio
                            className="ds-margin-top-lg"
                            value="TextInput"
                            isChecked={!uploadSelected}
                            onChange={this.handleRadioOnChange}
                            label="Write the complete file path."
                            description="Type the full path of the file on the server host the LDAP server."
                            name="radio-binary-attribute"
                            id="radio-type-binary-attribute"
                        />
                        <hr />
                        {/* binaryAttrMsg */}

                        {((fileName !== '') && encodedValueIsEmpty && strPathValueIsEmpty) &&
                            <Alert variant="danger" isInline title="There was an issue with the uploaded file ( incorrect type? )." />
                        }
                        {isFileTooLarge &&
                            <Alert variant="danger" isInline title="The file size larger than 128 MB. Use the file path option." />
                        }
                        {!attrIsJpegPhoto &&
                            <Label className="ds-margin-top-lg" icon={<InfoCircleIcon />} color="blue">
                                The certificate must be stored in the Distinguished Encoding Rules (DER) format.
                            </Label>
                        }
                        { uploadSelected &&
                            <FileUpload
                                id="file-upload"
                                type="dataURL"
                                className="ds-margin-top-lg"
                                filename={fileName}
                                value={fileName}
                                onFileInputChange={this.handleFileChange}
                                onClearClick={this.handleClear}
                                validated={uploadSelected && fileName === "" ? 'error' : 'default'}
                                hideDefaultPreview
                                browseButtonText="Choose Binary File"
                            />
                        }
                        { !uploadSelected &&
                            <TextInput
                                value={binaryAttributeFilePath}
                                onChange={this.handleBinaryAttributeInput}
                                isRequired
                                validated={file_is_path(binaryAttributeFilePath)
                                    ? ValidatedOptions.default
                                    : ValidatedOptions.error}
                                type="text"
                                aria-label="File input for a binary attribute."
                            />
                        }
                    </Modal>
                }
                <Table
                    actionResolver={this.actionResolver}
                    onRowEdit={this.updateEditableRows}
                    dropdownDirection="up"
                    aria-label="Editable Rows Table"
                    variant={TableVariant.compact}
                    cells={this.columns}
                    rows={tableRows}
                    className="ds-margin-top"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </React.Fragment>
        );
    }
}

EditableTable.propTypes = {
    wizardEntryDn: PropTypes.string,
    editableTableData: PropTypes.array,
    isAttributeSingleValued: PropTypes.func,
    isAttributeRequired: PropTypes.func,
    enableNextStep: PropTypes.func,
    saveCurrentRows: PropTypes.func,
    allObjectclasses: PropTypes.array,
    disableNamingChange: PropTypes.bool,
};

EditableTable.defaultProps = {
    wizardEntryDn: "",
    editableTableData: [],
    allObjectclasses: [],
    disableNamingChange: false
};

export default EditableTable;
