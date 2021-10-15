import React from 'react';
import {
  Alert, AlertGroup, AlertActionCloseButton,
  Modal, ModalVariant, Button,
  SimpleList, SimpleListItem, Divider, Label
} from '@patternfly/react-core';
import {
  generateRootEntryLdif, createLdapEntry, getRootSuffixEntryDetails
} from './utils.jsx';

class CreateRootSuffix extends React.Component {
  constructor (props) {
    super(props);
    this.state = {
      isModalOpen: false,
      ldifArray: []
    };

    this.createRootEntry = () => {
      console.log(`Creating the root entry ${this.props.suffixDn}`);
      const resultParams = {
        message: `The suffix ${this.props.suffixDn} was successfully created.`,
        variant: 'success'
      }
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
    }
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
    const ldifOk = ldifArray.length > 1 // ldifArray contains a single item in case of error.
    const ldifItems = ldifOk
      ? ldifArray.map((line, index) => {
        return (<SimpleListItem key={index}>{line}</SimpleListItem>)
      })
      : null;

    return (
      <Modal
        position="top"
        variant={ModalVariant.medium}
        title="Create Root Entry"
        isOpen={showEmptySuffixModal}
        onClose={handleEmptySuffixToggle}
        actions={[
          <Button
            key="create"
            variant="primary"
            isDisabled={!ldifOk}
            onClick={this.createRootEntry} // Calls this.props.handleEmptySuffixToggle()
          >
              Create
          </Button>,
          <Button
            key="cancel"
            variant="link"
            onClick={handleEmptySuffixToggle}>
              Cancel
          </Button>
        ]}
      >
        <Alert
          variant={ ldifOk ? 'info' : 'danger'}
          isInline
          title={ ldifOk
            ? 'LDIF data to create the suffix'
            : 'Failed to generate the LDIF data'
          }
        />
        <div className="ds-addons-bottom-margin" />
        {!ldifOk &&
          <Label variant="outline" color="red">
            {ldifArray[0]}
          </Label>
        }
        {ldifOk &&
        <React.Fragment>
          <Divider />
          <SimpleList aria-label="Simple List LDIF">
            {ldifItems}
          </SimpleList>
          <Divider />
        </React.Fragment>
        }
        <div className="ds-addons-bottom-margin" />
      </Modal>
    );
  }
}

export default CreateRootSuffix;
