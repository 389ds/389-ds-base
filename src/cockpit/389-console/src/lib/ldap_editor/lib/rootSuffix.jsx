import cockpit from "cockpit";
import React from 'react';
import {
    Alert,
    Button,
    Divider,
    Label,
    Modal,
    ModalVariant,
    SimpleList,
    SimpleListItem,
} from '@patternfly/react-core';
import {
    generateRootEntryLdif, createLdapEntry, getRootSuffixEntryDetails
} from './utils.jsx';

const _ = cockpit.gettext;

class CreateRootSuffix extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            isModalOpen: false,
            ldifArray: []
        };

        this.handleCreateRootEntry = () => {
            console.log(`Creating the root entry ${this.props.suffixDn}`);
            const resultParams = {
                message: cockpit.format(_("The suffix $0 was successfully created."), this.props.suffixDn),
                variant: 'success'
            };
            createLdapEntry(this.props.editorLdapServer, this.state.ldifArray,
                            (result) => {
                                //       result = { errorCode: err.exit_status, output: errMessage };
                                // If the creation succeeds, search the entry and update the entry table.
                                console.log(`result.errorCode = ${result.errorCode}`);
                                if (result.errorCode === 0) {
                                    const params = { serverId: this.props.editorLdapServer, baseDn: this.props.suffixDn };
                                    getRootSuffixEntryDetails(params, (entryInfo) => {
                                        const info = JSON.parse(entryInfo);
                                        /* const rootEntryData = {
                name: info.dn,
                fullEntry: info.fullEntry,
                modTime: info.modifyTimestamp,
                dn: info.dn
              }; */
                                        resultParams.rootEntryData = {
                                            name: info.dn,
                                            fullEntry: info.fullEntry,
                                            modTime: info.modifyTimestamp,
                                            dn: info.dn
                                        };
                                        // this.props.updateEntryRows(entryTreeData);
                                        // Hide the modal dialog and show the alert.
                                        this.props.handleEmptySuffixToggle(resultParams);
                                    });
                                } else {
                                    resultParams.message = `Failed to create the suffix ${this.props.suffixDn}.`;
                                    resultParams.variant = 'danger';
                                    // Hide the modal dialog and show the alert.
                                    this.props.handleEmptySuffixToggle(resultParams);
                                }
                            });
        };
    }

    componentDidMount () {
    // console.log(`In CreateRootSuffix - componentDidMount() - this.props.suffixDn = ${this.props.suffixDn}`);
        generateRootEntryLdif(this.props.suffixDn, (ldifArray) => {
            // console.log(ldifArray);
            this.setState({
                ldifArray
            });
        });
    }

    render () {
        const { showEmptySuffixModal, handleEmptySuffixToggle } = this.props;
        const { ldifArray } = this.state;
        const ldifOk = ldifArray.length > 1; // ldifArray contains a single item in case of error.
        const ldifItems = ldifOk
            ? ldifArray.map((line, index) => {
                return (<SimpleListItem key={index}>{line}</SimpleListItem>);
            })
            : null;

        return (
            <Modal
                position="top"
                variant={ModalVariant.medium}
                title={_("Create Root Entry")}
                isOpen={showEmptySuffixModal}
                onClose={handleEmptySuffixToggle}
                actions={[
                    <Button
                        key="create"
                        variant="primary"
                        isDisabled={!ldifOk}
                        onClick={this.handleCreateRootEntry} // Calls this.props.handleEmptySuffixToggle()
                    >
                        {_("Create")}
                    </Button>,
                    <Button
                        key="cancel"
                        variant="link"
                        onClick={handleEmptySuffixToggle}
                    >
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Alert
                    variant={ ldifOk ? 'info' : 'danger'}
                    isInline
                    title={ ldifOk
                        ? _("LDIF data to create the suffix")
                        : _("Failed to generate the LDIF data")}
                />
                <div className="ds-addons-bottom-margin" />
                {
                    !ldifOk &&
                    <Label variant="outline" color="red">
                        {ldifArray[0]}
                    </Label>
                }
                {
                    ldifOk &&
                    (
                        <>
                            <Divider />
                            <SimpleList aria-label="Simple List LDIF">
                                {ldifItems}
                            </SimpleList>
                            <Divider />
                        </>
                    )
                }
                <div className="ds-addons-bottom-margin" />
            </Modal>
        );
    }
}

export default CreateRootSuffix;
