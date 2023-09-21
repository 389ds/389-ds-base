import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import { LDIFTable } from "./databaseTables.jsx";
import PropTypes from "prop-types";

const _ = cockpit.gettext;

class CreateLinkModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            suffix,
            error,
            saving,
            handleSelectChange,
            starttls_checked,
            bindMech,
        } = this.props;

        let saveBtnName = _("Create Database Link");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Creating Link ...");
            extraPrimaryProps.spinnerAriaValueText = _("Creating");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Create Database Link")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? _("Creating Link") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top" title={_("The RDN of the link suffix.")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Link Sub-Suffix")}
                        </GridItem>
                        <GridItem span={9}>
                            <div className="ds-container">
                                <div>
                                    <TextInput
                                        type="text"
                                        className="ds-right-align"
                                        id="createLinkSuffix"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="createLinkSuffix"
                                        onChange={(checked, e) => {
                                            handleChange(e);
                                        }}
                                        validated={error.createLinkSuffix ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                </div>
                                <div className="ds-left-margin ds-lower-field-md">
                                    <b><font color="blue">,{suffix}</font></b>
                                </div>
                            </div>
                            <FormHelperText isError isHidden={!error.createLinkSuffix}>
                                {_("Required field")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title={_("A name for the backend chaining database link.")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Link Database Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="createLinkName"
                                aria-describedby="horizontal-form-name-helper"
                                name="createLinkName"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.createLinkName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!error.createLinkName}>
                                {_("Required field")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title={_("The LDAP URL for the remote server.  Add additional failover LDAP URLs separated by a space. (nsfarmserverurl).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Remote Server URL(s)")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="createNsfarmserverurl"
                                aria-describedby="horizontal-form-name-helper"
                                name="createNsfarmserverurl"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.createNsfarmserverurl ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!error.createNsfarmserverurl}>
                                {_("Required field")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title={_("The DN of the entry to authenticate with on the remote server.")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Remote Server Bind DN")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="createNsmultiplexorbinddn"
                                aria-describedby="horizontal-form-name-helper"
                                name="createNsmultiplexorbinddn"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.createNsmultiplexorbinddn ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!error.createNsmultiplexorbinddn}>
                                {_("Required field")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title={_("The credentials for the bind DN (nsmultiplexorcredentials).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Bind DN Credentials")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="password"
                                id="createNsmultiplexorcredentials"
                                aria-describedby="horizontal-form-name-helper"
                                name="createNsmultiplexorcredentials"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.createNsmultiplexorcredentials ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!error.createNsmultiplexorcredentials}>
                                {_("Password does not match")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title={_("Confirm credentials for the bind DN (nsmultiplexorcredentials).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Confirm Password")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="password"
                                id="createNsmultiplexorcredentialsConfirm"
                                aria-describedby="horizontal-form-name-helper"
                                name="createNsmultiplexorcredentialsConfirm"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.createNsmultiplexorcredentialsConfirm ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!error.createNsmultiplexorcredentialsConfirm}>
                                {_("Password does not match")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("The bind method for contacting the remote server  (nsbindmechanism).")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Bind Method")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect value={bindMech} onChange={handleSelectChange} aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="SIMPLE" label="SIMPLE" />
                                <FormSelectOption key={2} value="SASL/DIGEST-MD5" label="SASL/DIGEST-MD5" />
                                <FormSelectOption key={3} value="SASL/GSSAPI" label="SASL/GSSAPI" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title={_("Use StartTLS for the remote server LDAP URL.")}>
                        <GridItem span={12}>
                            <Checkbox
                                id="createUseStartTLS"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                isChecked={starttls_checked}
                                label={_("Use StartTLS")}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

class CreateSubSuffixModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            isOpen: false,
        };

        this.onToggle = isOpen => {
            this.setState({
                isOpen
            });
        };
    }

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            handleSelectChange,
            saveHandler,
            suffix,
            error,
            saving,
            initOption,
        } = this.props;

        let saveBtnName = _("Create Sub-Suffix");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Creating suffix ...");
            extraPrimaryProps.spinnerAriaValueText = _("Creating");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Create Sub Suffix")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? _("Creating Suffix") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top" title={_("Database suffix, like 'dc=example,dc=com'.  The suffix must be a valid LDAP Distiguished Name (DN)")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Sub-Suffix DN")}
                        </GridItem>
                        <GridItem span={9}>
                            <div className="ds-container">
                                <div>
                                    <TextInput
                                        type="text"
                                        className="ds-right-align"
                                        id="subSuffixValue"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="subSuffixValue"
                                        onChange={(val, e) => {
                                            handleChange(e);
                                        }}
                                        validated={error.subSuffixValue ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                </div>
                                <div className="ds-left-margin ds-lower-field-md">
                                    <b><font color="blue">,{suffix}</font></b>
                                </div>
                            </div>
                            <FormHelperText isError isHidden={!error.subSuffixValue}>
                                {_("Required field")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title={_("The name for the backend database, like 'userroot'.  The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends.")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Database Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="subSuffixBeName"
                                aria-describedby="horizontal-form-name-helper"
                                name="subSuffixBeName"
                                onChange={(val, e) => {
                                    handleChange(e);
                                }}
                                validated={error.subSuffixBeName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!error.subSuffixBeName}>
                                {_("Required field")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("Database initialization options")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Initialization Option")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect value={initOption} onChange={handleSelectChange} aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="noInit" label={_("Do Not Initialize Database")} />
                                <FormSelectOption key={2} value="addSuffix" label={_("Create The Top Sub-Suffix Entry")} />
                                <FormSelectOption key={3} value="addSample" label={_("Add Sample Entries")} />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

class ExportModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            includeReplData,
            saveHandler,
            spinning,
            error
        } = this.props;
        let saveBtnName = _("Export Database");
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = _("Exporting ...");
            extraPrimaryProps.spinnerAriaValueText = _("Creating");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Export Database To LDIF File")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="export"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? _("Creating Suffix") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid title={_("Name of exported LDIF file.")}>
                        <GridItem className="ds-label" span={3}>
                            {_("LDIF File Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="ldifLocation"
                                aria-describedby="horizontal-form-name-helper"
                                name="ldifLocation"
                                onChange={(val, e) => {
                                    handleChange(e);
                                }}
                                validated={error.ldifLocation ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("Include the replication metadata needed to restore or initialize another replica.")}>
                        <GridItem span={12}>
                            <Checkbox
                                id="includeReplData"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                isChecked={includeReplData}
                                label={_("Include Replication Data")}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

class ImportModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            rows,
            suffix
        } = this.props;

        const suffixRows = [];
        for (const row of rows) {
            if (row[3] === suffix) {
                suffixRows.push(row);
            }
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Initialize Database via LDIF File")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <LDIFTable
                    rows={suffixRows}
                    confirmImport={this.props.showConfirmImport}
                />
                <hr />
                <Form isHorizontal autoComplete="off">
                    <Grid title={_("The full path to the LDIF file.  The server must have permissions to read it.")}>
                        <GridItem className="ds-label" span={4}>
                            {_("Or, enter LDIF location")}
                        </GridItem>
                        <GridItem span={6}>
                            <TextInput
                                type="text"
                                id="ldifLocation"
                                aria-describedby="horizontal-form-name-helper"
                                name="ldifLocation"
                                onChange={(val, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                        <GridItem span={2}>
                            <Button
                                key="import"
                                className="ds-left-margin"
                                variant="primary"
                                onClick={saveHandler}
                                isDisabled={this.props.saveBtnDisabled}
                            >
                                {_("Import")}
                            </Button>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

// Property types and defaults

CreateLinkModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    suffix: PropTypes.string,
    error: PropTypes.object,
};

CreateLinkModal.defaultProps = {
    showModal: false,
    suffix: "",
    error: {},
};

CreateSubSuffixModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    suffix: PropTypes.string,
    error: PropTypes.object,
};

CreateSubSuffixModal.defaultProps = {
    showModal: false,
    suffix: "",
    error: {},
};

ExportModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    error: PropTypes.object,
    spinning: PropTypes.bool
};

ExportModal.defaultProps = {
    showModal: false,
    error: {},
    spinning: false
};

ImportModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    showConfirmImport: PropTypes.func,
    rows: PropTypes.array,
    suffix: PropTypes.string
};

ImportModal.defaultProps = {
    showModal: false,
    rows: [],
    suffix: ""
};

export {
    ImportModal,
    ExportModal,
    CreateSubSuffixModal,
    CreateLinkModal,
};
