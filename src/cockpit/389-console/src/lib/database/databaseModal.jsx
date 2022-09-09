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

        let saveBtnName = "Create Database Link";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Creating Link ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Create Database Link"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Creating Link" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top" title="The RDN of the link suffix.">
                        <GridItem className="ds-label" span={3}>
                            Link Sub-Suffix
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
                                Required field
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title="A name for the backend chaining database link.">
                        <GridItem className="ds-label" span={3}>
                            Link Database Name
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
                                Required field
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title="The LDAP URL for the remote server.  Add additional failover LDAP URLs separated by a space. (nsfarmserverurl).">
                        <GridItem className="ds-label" span={3}>
                            Remote Server URL(s)
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
                                Required field
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title="The DN of the entry to authenticate with on the remote server.">
                        <GridItem className="ds-label" span={3}>
                            Remote Server Bind DN
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
                                Required field
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title="The credentials for the bind DN (nsmultiplexorcredentials).">
                        <GridItem className="ds-label" span={3}>
                            Bind DN Credentials
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
                                Password does not match
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title="Confirm credentials for the bind DN (nsmultiplexorcredentials).">
                        <GridItem className="ds-label" span={3}>
                            Confirm Password
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
                                Password does not match
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The bind method for contacting the remote server  (nsbindmechanism)."
                    >
                        <GridItem className="ds-label" span={3}>
                            Bind Method
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect value={bindMech} onChange={handleSelectChange} aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="SIMPLE" label="SIMPLE" />
                                <FormSelectOption key={2} value="SASL/DIGEST-MD5" label="SASL/DIGEST-MD5" />
                                <FormSelectOption key={3} value="SASL/GSSAPI" label="SASL/GSSAPI" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Use StartTLS for the remote server LDAP URL.">
                        <GridItem span={12}>
                            <Checkbox
                                id="createUseStartTLS"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                isChecked={starttls_checked}
                                label="Use StartTLS"
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

        let saveBtnName = "Create Sub-Suffix";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Creating suffix ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Create Sub Suffix"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Creating Suffix" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top" title="Database suffix, like 'dc=example,dc=com'.  The suffix must be a valid LDAP Distiguished Name (DN)">
                        <GridItem className="ds-label" span={3}>
                            Sub-Suffix DN
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
                                Required field
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid title="The name for the backend database, like 'userroot'.  The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends.">
                        <GridItem className="ds-label" span={3}>
                            Database Name
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
                                Required field
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Database initialization options"
                    >
                        <GridItem className="ds-label" span={3}>
                            Initialization Option
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect value={initOption} onChange={handleSelectChange} aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="noInit" label="Do Not Initialize Database" />
                                <FormSelectOption key={2} value="addSuffix" label="Create The Top Sub-Suffix Entry" />
                                <FormSelectOption key={3} value="addSample" label="Add Sample Entries" />
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
        let saveBtnName = "Export Database";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Exporting ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Export Database To LDIF File"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="export"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Creating Suffix" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid title="Name of exported LDIF file.">
                        <GridItem className="ds-label" span={3}>
                            LDIF File Name
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
                    <Grid title="Include the replication metadata needed to restore or initialize another replica.">
                        <GridItem span={12}>
                            <Checkbox
                                id="includeReplData"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                isChecked={includeReplData}
                                label="Include Replication Data"
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
            if (row[3] == suffix) {
                suffixRows.push(row);
            }
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Initialize Database via LDIF File"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <LDIFTable
                    rows={suffixRows}
                    confirmImport={this.props.showConfirmImport}
                />
                <hr />
                <Form isHorizontal autoComplete="off">
                    <Grid title="The full path to the LDIF file.  The server must have permissions to read it.">
                        <GridItem className="ds-label" span={4}>
                            Or, enter LDIF location
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
                                Import
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
