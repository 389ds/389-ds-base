import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Checkbox,
	ClipboardCopy,
	ClipboardCopyVariant,
	Divider,
	ExpandableSection,
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
	Text,
	TextContent,
	TextVariants,
	TextInput,
	Tooltip,
	TooltipPosition,
	ValidatedOptions
} from '@patternfly/react-core';
import TypeaheadSelect from "../../dsBasicComponents.jsx";
import { OutlinedQuestionCircleIcon } from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';
import PropTypes from "prop-types";
import { bad_file_name, validHostname, file_is_path } from "../tools.jsx";

const _ = cockpit.gettext;

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

        let exportBtnName = _("Export Certificate");
        const extraPrimaryProps = {};
        if (spinning) {
            exportBtnName = _("Exporting Certificate ...");
            extraPrimaryProps.spinnerAriaValueText = _("Exporting");
        }

        const title = <>{_("Export Certificate:")} &nbsp;&nbsp;<i>{nickName}</i></>;
        const desc = <>{_("Enter the full path and file name, if the path portion is omitted the certificate is written to the server's certificate directory ")}<i>{certDir}</i></>;

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
                        spinnerAriaValueText={spinning ? _("Exporting") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={fileName === "" || spinning}
                    >
                        {exportBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
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
                            {_("Certificate File Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                title={_("Enter full path to and and including certificate file name")}
                                id="exportFileName"
                                aria-describedby="horizontal-form-name-helper"
                                name="exportFileName"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={fileName === "" ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                        <GridItem
                            title={_("Export certificate in its binary format.  Otherwise a PEM file is created.")}
                            className="ds-margin-top-lg" span={12}
                        >
                            <Checkbox
                                label={_("Export Certificate In Binary/DER Format")}
                                isChecked={binaryFormat}
                                onChange={(e, checked) => {
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
    constructor(props) {
        super(props);
        this.state = {
            isPasswordSectionExpanded: false
        };
        this.handlePasswordSectionToggle = this.handlePasswordSectionToggle.bind(this);
    }

    handlePasswordSectionToggle(event, isExpanded) {
        this.setState({
            isPasswordSectionExpanded: isExpanded
        });
    }

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            certName,
            certFile,
            certRadioFile,
            certRadioSelect,
            certRadioUpload,
            handleRadioChange,
            badCertText,
            certNames,
            certNicknames,
            CACertNicknames,
            // Select server cert
            handleCertSelect,
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
            // PKCS#12 Password options
            pkcs12PinMethod,
            pkcs12PinFile,
            pkcs12PinText,
            forceCertAdd,
            handlePkcs12PinMethodChange,
            handlePkcs12PinFileChange,
            handlePkcs12PinTextChange,
            handleForceCertAddChange,
        } = this.props;

        let saveBtnName = _("Add Certificate");
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = _("Adding Certificate ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        const certTextLabel = (
            <div>
                <Tooltip
                    content={
                        <div>
                            {_("Paste the base64 encoded certificate that starts with \"-----BEGIN CERTIFICATE-----\" and ends with \"-----END CERTIFICATE-----\".  Make sure there are no special carriage return characters after each line.")}
                        </div>
                    }
                >
                    <div>{_("Upload PEM File, or Certificate Text")} <OutlinedQuestionCircleIcon /></div>
                </Tooltip>
            </div>
        );

        let title = _("Add Server Certificate");
        let desc = _("Add a Server Certificate to the security database.");

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
                            saveHandler(false);
                        }}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={
                            certName === "" || (certRadioFile && certFile === "") ||
                            (certRadioUpload && (uploadValue === "" || badCertText)) ||
                            (certRadioSelect && certNames.length === 0) ||
                            (pkcs12PinMethod === "file" && (pkcs12PinFile === "" || !file_is_path(pkcs12PinFile))) ||
                            (pkcs12PinMethod === "stdin" && pkcs12PinText === "") ||
                            certNicknames.includes(certName) ||
                            CACertNicknames.includes(certName)
                        }
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
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
                        className="ds-margin-top-sm"
                        title={_("Enter name/nickname of the certificate")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Certificate Nickname")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certName"
                                aria-describedby="horizontal-form-name-helper"
                                name="certName"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={
                                    certName === "" ||
                                    certNicknames.includes(certName) ||
                                    CACertNicknames.includes(certName) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            {(certNicknames.includes(certName) || CACertNicknames.includes(certName)) && (
                                <HelperText>
                                    <HelperTextItem variant="error">
                                        {_("Please use a unique certificate nickname")}
                                    </HelperTextItem>
                                </HelperText>
                            )}
                        </GridItem>
                    </Grid>
                    <ExpandableSection
                        toggleText={this.state.isPasswordSectionExpanded ? _("Hide PKCS#12 Password Options") : _("Show PKCS#12 Password Options")}
                        onToggle={this.handlePasswordSectionToggle}
                        isExpanded={this.state.isPasswordSectionExpanded}
                    >
                        <div className="ds-indent-lg">
                            <TextContent className="ds-margin-top">
                                <Text component={TextVariants.p}>
                                    {_("These password settings are only used for PKCS#12 password protected certificates. These settings are ignored for other certificate types.")}
                                </Text>
                            </TextContent>
                            <Grid className="ds-margin-top-lg">
                                <GridItem span={12}>
                                    <Radio
                                        id="noPasswordRadio"
                                        label={_("Non-PKCS#12 Certificate")}
                                        name="pkcs12PinMethod"
                                        isChecked={pkcs12PinMethod === "noPassword"}
                                        onChange={handlePkcs12PinMethodChange}
                                    />
                                    <div
                                        className="ds-margin-top"
                                        title={_("Password will be read from stdin when prompted by the command")}
                                    >
                                        <Radio
                                            id="pkcs12PinRadioStdin"
                                            label={_("Password")}
                                            name="pkcs12PinMethod"
                                            isChecked={pkcs12PinMethod === "stdin"}
                                            onChange={handlePkcs12PinMethodChange}
                                        />
                                    </div>
                                    <div className="ds-margin-top ds-radio-indent">
                                        <TextInput
                                            type="password"
                                            id="pkcs12PinTextStdin"
                                            aria-describedby="pkcs12-pin-stdin-helper"
                                            name="pkcs12PinText"
                                            value={pkcs12PinText}
                                            onChange={(e, value) => {
                                                handlePkcs12PinTextChange(value);
                                            }}
                                            placeholder={_("Enter password to send via stdin")}
                                            validated={pkcs12PinMethod === "stdin" && pkcs12PinText === "" ? ValidatedOptions.error : ValidatedOptions.default}
                                            isDisabled={pkcs12PinMethod !== "stdin"}
                                        />
                                    </div>
                                    <div
                                        className="ds-margin-top"
                                        title={_("Read password from a file on the server")}
                                    >
                                        <Radio
                                            id="pkcs12PinRadioFile"
                                            className="ds-margin-top-lg"
                                            label={_("Read password from file")}
                                            name="pkcs12PinMethod"
                                            isChecked={pkcs12PinMethod === "file"}
                                            onChange={handlePkcs12PinMethodChange}
                                        />
                                    </div>
                                    <div className="ds-margin-top ds-radio-indent">
                                        <TextInput
                                            type="text"
                                            id="pkcs12PinFile"
                                            aria-describedby="pkcs12-pin-file-helper"
                                            name="pkcs12PinFile"
                                            value={pkcs12PinFile}
                                            onChange={(e, value) => {
                                                handlePkcs12PinFileChange(value);
                                            }}
                                            placeholder={_("Enter full path to password file")}
                                            isDisabled={pkcs12PinMethod !== "file"}
                                            validated={
                                                pkcs12PinMethod === "file" &&
                                                (pkcs12PinFile === "" || !file_is_path(pkcs12PinFile))
                                                    ? ValidatedOptions.error
                                                    : ValidatedOptions.default
                                            }
                                        />
                                        {pkcs12PinMethod === "file" && pkcs12PinFile !== "" && !file_is_path(pkcs12PinFile) && (
                                            <HelperText>
                                                <HelperTextItem variant="error">
                                                    {_("Please enter a valid file path (must start with '/' and not end with '/')")}
                                                </HelperTextItem>
                                            </HelperText>
                                        )}
                                    </div>
                                    <div className="ds-margin-top-xxlg">
                                        <Tooltip
                                            position={TooltipPosition.bottomStart}
                                            content={
                                                <div>
                                                    {_("Force certificate addition without validation. This bypasses certificate chain validation checks.")}
                                                </div>
                                            }
                                        >
                                            <Checkbox
                                                id="forceCertAdd"
                                                className="ds-margin-top-lg"
                                                label={_("Skip certificate verification")}
                                                isChecked={forceCertAdd}
                                                onChange={(e, checked) => {
                                                    handleForceCertAddChange(checked);
                                                }}
                                            />
                                        </Tooltip>
                                    </div>
                                </GridItem>
                            </Grid>
                        </div>
                    </ExpandableSection>
                    <Grid>
                        <GridItem span={12}>
                            <div title={_("Upload the contents of a PEM file from the client's system.")}>
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
                                    filenamePlaceholder={_("Drag and drop a file, or upload one")}
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
                                        (certRadioUpload && badCertText)
                                            ? 'error'
                                            : 'default'
                                    }
                                    browseButtonText={_("Upload PEM File")}
                                />
                            </div>
                            <div title={_("Choose a certificate from the server's certificate directory")}>
                                <Radio
                                    id="certRadioSelect"
                                    className="ds-margin-top-lg"
                                    label={_("Choose Certificate From Server")}
                                    name="certChoice"
                                    isChecked={certRadioSelect}
                                    onChange={handleRadioChange}
                                />
                            </div>
                            <div className={certRadioSelect ? "ds-margin-top ds-radio-indent" : "ds-margin-top ds-radio-indent ds-disabled"}>
                                <FormSelect
                                    value={selectCertName}
                                    id="selectCertName"
                                    onChange={(e, str) => {
                                        handleCertSelect(str);
                                    }}
                                    aria-label="FormSelect Input"
                                    className="ds-cert-select"
                                    validated={selectValidated}
                                >
                                    {certNames.length === 0 &&
                                        <FormSelectOption
                                            key="none"
                                            value=""
                                            label={_("No certificates present")}
                                            isDisabled
                                            isPlaceholder
                                        />}
                                    {certNames.length > 0 && certNames.map((option, index) => (
                                        <FormSelectOption
                                            key={index}
                                            value={option}
                                            label={option}
                                        />
                                    ))}
                                </FormSelect>
                            </div>
                            <div title={_("Enter the full path on the server to and including the certificate file name")}>
                                <Radio
                                    id="certRadioFile"
                                    className="ds-margin-top-lg"
                                    label={_("Certificate File Location")}
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
                                    onChange={(e, value) => {
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

export class SecurityAddCACertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            certName,
            certFile,
            certRadioFile,
            certRadioSelect,
            certRadioUpload,
            handleRadioChange,
            badCertText,
            certNames,
            certNicknames,
            CACertNicknames,
            // Select server cert
            handleCertSelect,
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
        } = this.props;

        let saveBtnName = _("Add Certificate");
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = _("Adding Certificate ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        const certTextLabel = (
            <div>
                <Tooltip
                    content={
                        <div>
                            {_("Paste the base64 encoded certificate that starts with \"-----BEGIN CERTIFICATE-----\" and ends with \"-----END CERTIFICATE-----\".  Make sure there are no special carriage return characters after each line.")}
                        </div>
                    }
                >
                    <div>{_("Upload PEM File, or Certificate Text")} <OutlinedQuestionCircleIcon /></div>
                </Tooltip>
            </div>
        );


        let title = _("Add Certificate Authority");
        let desc = _("Add a CA Certificate to the security database.");
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
                            saveHandler(true);
                        }}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={
                            certName === "" || (certRadioFile && certFile === "") ||
                            (certRadioUpload && (uploadValue === "" || badCertText)) ||
                            (certRadioSelect && certNames.length === 0) ||
                            certNicknames.includes(certName) ||
                            CACertNicknames.includes(certName)
                        }
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
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
                        className="ds-margin-top-sm"
                        title={_("Enter name/nickname of the certificate")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Certificate Nickname")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certName"
                                aria-describedby="horizontal-form-name-helper"
                                name="certName"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={
                                    certName === "" ||
                                    certNicknames.includes(certName) ||
                                    CACertNicknames.includes(certName) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            {(certNicknames.includes(certName) || CACertNicknames.includes(certName)) && (
                                <HelperText>
                                    <HelperTextItem variant="error">
                                        {_("Please use a unique certificate nickname")}
                                    </HelperTextItem>
                                </HelperText>
                            )}
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top-lg">
                        <GridItem span={12}>
                            <div title={_("Upload the contents of a PEM file from the client's system.")}>
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
                                    filenamePlaceholder={_("Drag and drop a file, or upload one")}
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
                                        (certRadioUpload && badCertText)
                                            ? 'error'
                                            : 'default'
                                    }
                                    browseButtonText={_("Upload PEM File")}
                                />
                            </div>
                            <div title={_("Choose a certificate from the server's certificate directory")}>
                                <Radio
                                    id="certRadioSelect"
                                    className="ds-margin-top-lg"
                                    label={_("Choose Certificate From Server")}
                                    name="certChoice"
                                    isChecked={certRadioSelect}
                                    onChange={handleRadioChange}
                                />
                            </div>
                            <div className={certRadioSelect ? "ds-margin-top ds-radio-indent" : "ds-margin-top ds-radio-indent ds-disabled"}>
                                <FormSelect
                                    value={selectCertName}
                                    id="selectCertName"
                                    onChange={(e, str) => {
                                        handleCertSelect(str);
                                    }}
                                    aria-label="FormSelect Input"
                                    className="ds-cert-select"
                                    validated={selectValidated}
                                >
                                    {certNames.length === 0 &&
                                        <FormSelectOption
                                            key="none"
                                            value=""
                                            label={_("No certificates present")}
                                            isDisabled
                                            isPlaceholder
                                        />}
                                    {certNames.length > 0 && certNames.map((option, index) => (
                                        <FormSelectOption
                                            key={index}
                                            value={option}
                                            label={option}
                                        />
                                    ))}
                                </FormSelect>
                            </div>
                            <div title={_("Enter the full path on the server to and including the certificate file name")}>
                                <Radio
                                    id="certRadioFile"
                                    className="ds-margin-top-lg"
                                    label={_("Certificate File Location")}
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
                                    onChange={(e, value) => {
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

const EMPTY_OPTIONS = [];

export class SecurityAddCSRModal extends React.Component {
    validateCreateHostname = (hostname) => {
        return validHostname(hostname) !== null;
    };

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
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

        let saveBtnName = _("Create Certificate Signing Request");
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = _("Creating Certificate Signing Request ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        let validAltNames = true;
        let invalidNames = "";
        for (const hostname of csrAltNames) {
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
                title={_("Create Certificate Signing Request")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={error.csrName || bad_file_name(csrName) || error.csrSubjectCommonName || spinning || !validAltNames}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form className="ds-margin-top" isHorizontal autoComplete="off">
                    <Grid title={_("CSR Name")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("Name used to identify a CSR")}
                                type="text"
                                id="csrName"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrName"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={error.csrName || bad_file_name(csrName) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject alternative host names")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Subject Alternative Names")}
                        </GridItem>
                        <GridItem span={9}>
                            <TypeaheadSelect
                                selected={csrAltNames}
                                onSelect={handleOnSelect}
                                options={EMPTY_OPTIONS}
                                isOpen={csrIsSelectOpen}
                                onToggle={handleOnToggle}
                                placeholder={_("Type an alternative host name")}
                                ariaLabel="Type a host name"
                                isMulti={true}
                                isCreatable={true}
                                validateCreate={this.validateCreateHostname}
                                validated={validAltNames ? "default" : "error"}
                            />
                            <div className={validAltNames ? "ds-hidden" : ""}>
                                <HelperText>
                                    <HelperTextItem variant="error">{_("Invalid host names: ")}{invalidNames}</HelperTextItem>
                                </HelperText>
                            </div>
                        </GridItem>
                    </Grid>
                    <Divider />
                    <Grid title={_("CSR Subject: Common Name")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Common Name (CN)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("The fully qualified domain name (FQDN) of your server")}
                                type="text"
                                id="csrSubjectCommonName"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectCommonName"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                                validated={error.csrSubjectCommonName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject: Organisation")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Organization (O)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("The legal name of your organization")}
                                type="text"
                                id="csrSubjectOrg"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectOrg"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject: Organisational Unit")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Organizational Unit (OU)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("The division of your organization handling the certificate")}
                                type="text"
                                id="csrSubjectOrgUnit"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectOrgUnit"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject: City/Locality")}>
                        <GridItem className="ds-label" span={3}>
                            {_("City/Locality (L)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("The city where your organization is located")}
                                type="text"
                                id="csrSubjectLocality"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectLocality"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject: State/Region")}>
                        <GridItem className="ds-label" span={3}>
                            {_("State/County/Region (ST)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("The state/region where your organization is located")}
                                type="text"
                                id="csrSubjectState"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectState"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject: Country Code")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Country Code (C)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("Two-letter country code where organization is located")}
                                type="text"
                                id="csrSubjectCountry"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectCountry"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("CSR Subject: Email Address")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Email Address")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title={_("Email address used to contact your organization")}
                                type="text"
                                id="csrSubjectEmail"
                                aria-describedby="horizontal-form-name-helper"
                                name="csrSubjectEmail"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Divider />
                    <Grid>
                        <GridItem span={3}>
                            {_("Computed Subject")}
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
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <TextContent title={_("CSR content")}>
                    <Text component={TextVariants.pre}>
                        <Text component={TextVariants.small}>
                            <ClipboardCopy hoverTip={_("Copy to clipboard")} clickTip="Copied" variant={ClipboardCopyVariant.expansion} isBlock>
                                {item || _("Nothing to display")}
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

        let saveBtnName = _("Enable Security");
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = _("Enabling Security ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <Modal
                variant={ModalVariant.small}
                aria-labelledby="ds-modal"
                title={_("Enable Security")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h4}>
                            {_("You are choosing to enable security for the Directory Server which allows the server to accept incoming client TLS connections.  Please select which certificate the server should use.")}
                        </Text>
                    </TextContent>
                    <hr />
                    <Grid className="ds-margin-top" title={_("The server certificates the Directory Server can use")}>
                        <GridItem className="ds-label" span={4}>
                            {_("Available Certificates")}
                        </GridItem>
                        <GridItem sm={8}>
                            <FormSelect
                                value={primaryName}
                                id="certNameSelect"
                                onChange={(e, str) => {
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

        let saveBtnName = _("Save Flags");
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = _("Saving flags ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
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

        if (flags !== "") {
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

        const title = _("Edit Certificate Trust Flags (") + this.props.flags + ")";

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
                        spinnerAriaValueText={spinning ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.disableSaveBtn || spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Grid className="ds-margin-top">
                    <GridItem span={6}>
                        {_("Flags")}
                    </GridItem>
                    <GridItem span={2}>
                        {_("SSL")}
                    </GridItem>
                    <GridItem span={2}>
                        {_("Email")}
                    </GridItem>
                    <GridItem span={2}>
                        {_("Object Signing")}
                    </GridItem>
                    <hr />
                    <GridItem span={6} title={_("Trusted CA (flag 'C', also implies 'c' flag)")}>
                        {_("(C) - Trusted CA")}
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagSSL"
                            isChecked={CSSLChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagEmail"
                            isChecked={CEmailChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagOS"
                            isChecked={COSChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title={_("Trusted CA for client authentication (flag 'T')")}>
                        {_("(T) - Trusted CA Client Auth")}
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagSSL"
                            isChecked={TSSLChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagEmail"
                            isChecked={TEmailChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagOS"
                            isChecked={TOSChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title={_("Valid CA (flag 'c')")}>
                        {_("(c) - Valid CA")}
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagSSL"
                            isChecked={cSSLChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagEmail"
                            isChecked={cEmailChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagOS"
                            isChecked={cOSChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title={_("Trusted Peer (flag 'P', implies flag 'p')")}>
                        {_("(P) - Trusted Peer")}
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagSSL"
                            isChecked={PSSLChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagEmail"
                            isChecked={PEmailChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagOS"
                            isChecked={POSChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title={_("Valid Peer (flag 'p')")}>
                        {_("(p) - Valid Peer")}
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagSSL"
                            isChecked={pSSLChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagEmail"
                            isChecked={pEmailChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagOS"
                            isChecked={pOSChecked}
                            onChange={(e, checked) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <hr />
                    <GridItem span={6} title={_("A private key is associated with the certificate. This is a dynamic flag and you cannot adjust it.")}>
                        {_("(u) - Private Key")}
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

SecurityAddCACertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

SecurityAddCACertModal.defaultProps = {
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
};

SecurityViewCSRModal.defaultProps = {
    showModal: false,
    spinning: false,
};
