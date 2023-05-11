import React from "react";
import {
    Button,
    Checkbox,
    ClipboardCopy, ClipboardCopyVariant, Card, CardBody, CardFooter, CardTitle,
    Divider,
    FileUpload,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    HelperText,
    HelperTextItem,
    Modal,
    ModalVariant,
    Radio,
    Select,
    SelectOption,
    SelectVariant,
    Text,
    TextArea,
    TextContent,
    TextVariants,
    TextInput,
    Tooltip,
    ValidatedOptions,
} from "@patternfly/react-core";
import OutlinedQuestionCircleIcon from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';
import PropTypes from "prop-types";
import { bad_file_name, validHostname } from "../tools.jsx";

export class ExportCertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            fileName,
            nickName,
            binaryFormat,
            certDir,
            spinning,
        } = this.props;

        let exportBtnName = "Export Certificate";
        const extraPrimaryProps = {};
        if (spinning) {
            exportBtnName = "Exporting Certificate ...";
            extraPrimaryProps.spinnerAriaValueText = "Exporting";
        }

        const title = <>Export Certificate: &nbsp;&nbsp;<i>{nickName}</i></>;
        const desc = <>Enter the full path and file name, if the path portion is omitted the cetificate is written to the server's certificate directory <i>{certDir}</i></>

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Exporting" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={fileName === "" || spinning}
                    >
                        {exportBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off" className="ds-margin-top-lg">
                    <Grid>
                        <GridItem className="ds-label" span={12}>
                            {desc}
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Certificate File Name
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                title="Enter full path to and and including certificate file name"
                                id="exportFileName"
                                aria-describedby="horizontal-form-name-helper"
                                name="exportFileName"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={fileName === "" ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                        <GridItem
                            title="Export certificate in its binary format.  Otherwise a PEM file is created."
                            className="ds-margin-top-lg" span={12}
                        >
                            <Checkbox
                                label="Export Certificate In Binary/DER Format"
                                isChecked={binaryFormat}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                id="exportDERFormat"
                                name="binaryFormat"
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class SecurityAddCertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            certName,
            certFile,
            certText,
            certRadioFile,
            certRadioSelect,
            certRadioUpload,
            handleRadioChange,
            badCertText,
            certNames,
            // Select server cert
            handleCertSelect,
            handleCertToggle,
            isSelectCertOpen,
            selectCertName,
            // File Upload
            uploadValue,
            uploadFileName,
            uploadIsLoading,
            uploadIsRejected,
            handleFileInputChange,
            handleTextOrDataChange,
            handleFileReadStarted,
            handleFileReadFinished,
            handleClear,
            handleFileRejected,
            isCACert,
        } = this.props;

        let saveBtnName = "Add Certificate";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Adding Certificate ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        const certTextLabel =
            <div>
                <Tooltip
                    content={
                        <div>
                            Paste the base64 encoded certificate that
                            starts with "-----BEGIN CERTIFICATE-----"
                            and ends with "-----END CERTIFICATE-----".
                            Make sure there are no special carriage return
                            characters after each line.
                        </div>
                      }
                >
                    <div>Upload PEM File, or Certificate Text <OutlinedQuestionCircleIcon /></div>
                </Tooltip>
            </div>;


        let title = "Add Server Certificate";
        let desc = "Add a Server Certificate to the security database.";
        if (isCACert) {
            title = "Add Certificate Authority";
            desc = "Add a CA Certificate to the security database.";
        }

        let selectValidated = ValidatedOptions.default;
        if (certRadioSelect && certNames.length === 0) {
            selectValidated = ValidatedOptions.error;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={() => {
                            saveHandler(isCACert);
                        }}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={
                            certName === "" || (certRadioFile && certFile === "") ||
                            (certRadioUpload && (uploadValue === "" || badCertText)) ||
                            (certRadioSelect && certNames.length === 0)
                        }
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h4}>
                            {desc}
                        </Text>
                    </TextContent>
                    <Grid
                        className="ds-margin-top-lg"
                        title="Enter name/nickname of the certificate"
                    >
                        <GridItem className="ds-label" span={3}>
                            Certificate Nickname
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certName"
                                aria-describedby="horizontal-form-name-helper"
                                name="certName"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={certName === "" ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" >
                        <GridItem span={12}>
                            <div title="Upload the contents of a PEM file from the client's system.">
                                <Radio
                                    id="certRadioUpload"
                                    label={certTextLabel}
                                    name="certChoice"
                                    onChange={handleRadioChange}
                                    isChecked={certRadioUpload}
                                />
                            </div>
                            <div className={certRadioUpload ? "ds-margin-top ds-radio-indent" : "ds-margin-top ds-radio-indent ds-disabled"}>
                                <FileUpload
                                    id="uploadPEMFile"
                                    type="text"
                                    value={uploadValue}
                                    filename={uploadFileName}
                                    filenamePlaceholder="Drag and drop a file, or upload one"
                                    onFileInputChange={handleFileInputChange}
                                    onDataChange={handleTextOrDataChange}
                                    onTextChange={handleTextOrDataChange}
                                    onReadStarted={handleFileReadStarted}
                                    onReadFinished={handleFileReadFinished}
                                    onClearClick={handleClear}
                                    isLoading={uploadIsLoading}
                                    dropzoneProps={{
                                        accept: '.pem',
                                        onDropRejected: handleFileRejected
                                    }}
                                    validated={
                                        uploadIsRejected ||
                                        (certRadioUpload && uploadValue === "") ||
                                        (certRadioUpload && badCertText) ? 'error' : 'default'
                                    }
                                    browseButtonText="Upload PEM File"
                                />
                            </div>
                            <div title="Choose a cerificate from the server's certificate directory">
                                <Radio
                                    id="certRadioSelect"
                                    className="ds-margin-top-lg"
                                    label="Choose Cerificate From Server"
                                    name="certChoice"
                                    isChecked={certRadioSelect}
                                    onChange={handleRadioChange}
                                />
                            </div>
                            <div className={certRadioSelect ? "ds-margin-top ds-radio-indent" : "ds-margin-top ds-radio-indent ds-disabled"}>
                                <FormSelect
                                    value={selectCertName}
                                    id="selectCertName"
                                    onChange={handleCertSelect}
                                    aria-label="FormSelect Input"
                                    className="ds-cert-select"
                                    validated={selectValidated}
                                >
                                    {certNames.length === 0 &&
                                        <FormSelectOption
                                            key="none"
                                            value=""
                                            label="No certificates present"
                                            isDisabled
                                            isPlaceholder
                                        />
                                    }
                                    {certNames.length > 0 && certNames.map((option, index) => (
                                        <FormSelectOption
                                            key={index}
                                            value={option}
                                            label={option}
                                        />
                                    ))}
                                </FormSelect>
                            </div>
                            <div title="Enter the full path on the server to and including the certificate file name">
                                <Radio
                                    id="certRadioFile"
                                    className="ds-margin-top-lg"
                                    label="Certificate File Location"
                                    name="certChoice"

                                    isChecked={certRadioFile}
                                    onChange={handleRadioChange}
                                />
                            </div>
                            <div className={certRadioFile ? "ds-margin-top ds-radio-indent" : "ds-margin-top ds-radio-indent ds-disabled"}>
                                <TextInput
                                    type="text"
                                    id="certFile"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="certFile"
                                    onChange={(value, e) => {
                                        handleChange(e);
                                    }}
                                    validated={certRadioFile && certFile === "" ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </div>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class SecurityAddCSRModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            handleAltNameChange,
            saveHandler,
            previewValue,
            spinning,
            csrName,
            csrAltNames,
            csrIsSelectOpen,
            handleOnToggle,
            handleOnSelect,
            error
        } = this.props;

        let saveBtnName = "Create Certificate Signing Request";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Creating Certificate Signing Request ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        let validAltNames = true;
        let invalidNames = "";
        let index = 0;
        for (const hostname of csrAltNames) {
            index += 1;
            if (!validHostname(hostname)) {
                validAltNames = false;
                if (invalidNames === "") {
                    invalidNames += '"' + hostname + '"';
                } else {
                    invalidNames += ', "' + hostname + '"';
                }
            }
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title="Create Certificate Signing Request"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={error.csrName || bad_file_name(csrName) || error.csrSubjectCommonName || spinning || !validAltNames}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form className="ds-margin-top" isHorizontal autoComplete="off">
                    <Grid title="CSR Name">
                        <GridItem className="ds-label" span={3}>
                            Name
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="Name used to identify a CSR"
                                type="text"
                                id="csrName"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrName"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.csrName || bad_file_name(csrName) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject alternative host names">
                        <GridItem className="ds-label" span={3}>
                            Subject Alternative Names
                        </GridItem>
                        <GridItem span={9}>
                            <Select
                                variant={SelectVariant.typeaheadMulti}
                                typeAheadAriaLabel="Type a host name"
                                onToggle={handleOnToggle}
                                onSelect={handleOnSelect}
                                selections={csrAltNames}
                                aria-labelledby="typeAhead-alt-names"
                                placeholderText="Type an alternative host name"
                                isOpen={csrIsSelectOpen}
                                isCreatable
                                isCreateOptionOnTop
                                onCreateOption={handleAltNameChange}
                                validated={validAltNames ? ValidatedOptions.default : ValidatedOptions.error}
                            >
                                {csrAltNames.map((hostname, index) => (
                                    <SelectOption
                                        key={index}
                                        value={hostname}
                                    />
                                ))}
                            </Select>
                            <div className={validAltNames ? "ds-hidden" : ""}>
                                <HelperText>
                                    <HelperTextItem variant="error">Invalid host names: {invalidNames}</HelperTextItem>
                                </HelperText>
                            </div>
                        </GridItem>
                    </Grid>
                    <Divider />
                    <Grid title="CSR Subject: Common Name">
                        <GridItem className="ds-label" span={3}>
                            Common Name (CN)
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="The fully qualified domain name (FQDN) of your server"
                                type="text"
                                id="csrSubjectCommonName"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectCommonName"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.csrSubjectCommonName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject: Organisation">
                        <GridItem className="ds-label" span={3}>
                            Organization (O)
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="The legal name of your organization"
                                type="text"
                                id="csrSubjectOrg"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectOrg"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject: Organisational Unit">
                        <GridItem className="ds-label" span={3}>
                            Organizational Unit (OU)
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="The division of your organization handling the certificate"
                                type="text"
                                id="csrSubjectOrgUnit"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectOrgUnit"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject: City/Locality">
                        <GridItem className="ds-label" span={3}>
                            City/Locality (L)
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="The city where your organization is located"
                                type="text"
                                id="csrSubjectLocality"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectLocality"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject: State/Region">
                        <GridItem className="ds-label" span={3}>
                            State/County/Region (ST)
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="The state/region where your organization is located"
                                type="text"
                                id="csrSubjectState"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectState"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject: Country Code">
                        <GridItem className="ds-label" span={3}>
                            Country Code (C)
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="Two-letter country code where organization is located"
                                type="text"
                                id="csrSubjectCountry"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectCountry"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="CSR Subject: Email Address">
                        <GridItem className="ds-label" span={3}>
                            Email Address
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="Email address used to contact your organization"
                                type="text"
                                id="csrSubjectEmail"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectEmail"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Divider />
                    <Grid>
                        <GridItem span={3}>
                            Computed Subject
                        </GridItem>
                        <GridItem span={9}>
                            <b>{previewValue}</b>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class SecurityViewCSRModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            item,
            name,
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={name + ".csr"}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <TextContent title="CSR content">
                    <Text component={TextVariants.pre}>
                        <Text component={TextVariants.small}>
                            <ClipboardCopy hoverTip="Copy to clipboard" clickTip="Copied" variant={ClipboardCopyVariant.expansion} isBlock>
                                {item ? item : "Nothing to display"}
                            </ClipboardCopy>
                        </Text>
                    </Text>
                </TextContent>
            </Modal>
        );
    }
}

export class SecurityEnableModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            primaryName,
            certs,
            spinning
        } = this.props;

        // Build list of cert names for the select list
        const certNames = [];
        for (const cert of certs) {
            certNames.push(cert.attrs.nickname);
        }

        let saveBtnName = "Enable Security";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Enabling Security ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <Modal
                variant={ModalVariant.small}
                aria-labelledby="ds-modal"
                title="Enable Security"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h4}>
                            You are choosing to enable security for the Directory Server which
                            allows the server to accept incoming client TLS connections.  Please
                            select which certificate the server should use.
                        </Text>
                    </TextContent>
                    <hr />
                    <Grid className="ds-margin-top" title="The server certificates the Directory Server can use">
                        <GridItem className="ds-label" span={4}>
                            Available Certificates
                        </GridItem>
                        <GridItem sm={8}>
                            <FormSelect
                                value={primaryName}
                                id="certNameSelect"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                aria-label="FormSelect Input"
                            >
                                {certNames.map((option) => (
                                    <FormSelectOption key={option} value={option} label={option} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class EditCertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            flags,
            spinning
        } = this.props;

        let saveBtnName = "Save Flags";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Saving flags ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        // Process the cert flags
        let CSSLChecked = false;
        let CEmailChecked = false;
        let COSChecked = false;
        let TSSLChecked = false;
        let TEmailChecked = false;
        let TOSChecked = false;
        let cSSLChecked = false;
        let cEmailChecked = false;
        let cOSChecked = false;
        let PSSLChecked = false;
        let PEmailChecked = false;
        let POSChecked = false;
        let pSSLChecked = false;
        let pEmailChecked = false;
        let pOSChecked = false;
        let uSSLChecked = false;
        let uEmailChecked = false;
        let uOSChecked = false;
        let SSLFlags = '';
        let EmailFlags = '';
        let OSFlags = '';

        if (flags != "") {
            [SSLFlags, EmailFlags, OSFlags] = flags.split(',');
            if (SSLFlags.includes('T')) {
                TSSLChecked = true;
            }
            if (EmailFlags.includes('T')) {
                TEmailChecked = true;
            }
            if (OSFlags.includes('T')) {
                TOSChecked = true;
            }
            if (SSLFlags.includes('C')) {
                CSSLChecked = true;
            }
            if (EmailFlags.includes('C')) {
                CEmailChecked = true;
            }
            if (OSFlags.includes('C')) {
                COSChecked = true;
            }
            if (SSLFlags.includes('c')) {
                cSSLChecked = true;
            }
            if (EmailFlags.includes('c')) {
                cEmailChecked = true;
            }
            if (OSFlags.includes('c')) {
                cOSChecked = true;
            }
            if (SSLFlags.includes('P')) {
                PSSLChecked = true;
            }
            if (EmailFlags.includes('P')) {
                PEmailChecked = true;
            }
            if (OSFlags.includes('P')) {
                POSChecked = true;
            }
            if (SSLFlags.includes('p')) {
                pSSLChecked = true;
            }
            if (EmailFlags.includes('p')) {
                pEmailChecked = true;
            }
            if (OSFlags.includes('p')) {
                pOSChecked = true;
            }
            if (SSLFlags.includes('u')) {
                uSSLChecked = true;
            }
            if (EmailFlags.includes('u')) {
                uEmailChecked = true;
            }
            if (OSFlags.includes('u')) {
                uOSChecked = true;
            }
        }

        const title = "Edit Certificate Trust Flags (" + this.props.flags + ")";

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.disableSaveBtn || spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Grid className="ds-margin-top">
                    <GridItem span={6}>
                        Flags
                    </GridItem>
                    <GridItem span={2}>
                        SSL
                    </GridItem>
                    <GridItem span={2}>
                        Email
                    </GridItem>
                    <GridItem span={2}>
                        Object Signing
                    </GridItem>
                    <hr />
                    <GridItem span={6} title="Trusted CA (flag 'C', also implies 'c' flag)">
                        (C) - Trusted CA
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagSSL"
                            isChecked={CSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagEmail"
                            isChecked={CEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagOS"
                            isChecked={COSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Trusted CA for client authentication (flag 'T')">
                        (T) - Trusted CA Client Auth
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagSSL"
                            isChecked={TSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagEmail"
                            isChecked={TEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagOS"
                            isChecked={TOSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Valid CA (flag 'c')">
                        (c) - Valid CA
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagSSL"
                            isChecked={cSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagEmail"
                            isChecked={cEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagOS"
                            isChecked={cOSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Trusted Peer (flag 'P', implies flag 'p')">
                        (P) - Trusted Peer
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagSSL"
                            isChecked={PSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagEmail"
                            isChecked={PEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagOS"
                            isChecked={POSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Valid Peer (flag 'p')">
                        (p) - Valid Peer
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagSSL"
                            isChecked={pSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagEmail"
                            isChecked={pEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagOS"
                            isChecked={pOSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <hr />
                    <GridItem span={6} title="A private key is associated with the certificate. This is a dynamic flag and you cannot adjust it.">
                        (u) - Private Key
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="uflagSSL"
                            isChecked={uSSLChecked}
                            isDisabled
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="uflagEmail"
                            isChecked={uEmailChecked}
                            isDisabled
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="uflagOS"
                            isChecked={uOSChecked}
                            isDisabled
                        />
                    </GridItem>
                </Grid>
            </Modal>
        );
    }
}

SecurityEnableModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    primaryName: PropTypes.string,
    certs: PropTypes.array,
    spinning: PropTypes.bool,
};

SecurityEnableModal.defaultProps = {
    showModal: false,
    primaryName: "",
    certs: [],
    spinning: false,
};

EditCertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    flags: PropTypes.string,
    spinning: PropTypes.bool,
};

EditCertModal.defaultProps = {
    showModal: false,
    flags: "",
    spinning: false,
};

ExportCertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    binaryFormat: PropTypes.bool,
    fileName: PropTypes.string,
    spinning: PropTypes.bool,
};

ExportCertModal.defaultProps = {
    showModal: false,
    fileName: "",
    binaryFormat: false,
    spinning: false,
};

SecurityAddCertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

SecurityAddCertModal.defaultProps = {
    showModal: false,
    spinning: false,
    saveHandler: () => {},
    error: {},
};

SecurityAddCSRModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

SecurityAddCSRModal.defaultProps = {
    showModal: false,
    spinning: false,
    error: {},
};

SecurityViewCSRModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

SecurityViewCSRModal.defaultProps = {
    showModal: false,
    spinning: false,
    error: {},
};
