import React from 'react';
import AciWizard from './aci.jsx';
import CoSEntryWizard from './cos.jsx';
import NewEntryWizard from './newEntry.jsx';
import EditLdapEntry from './operations/editLdapEntry.jsx';
import RenameEntry from './operations/renameEntry.jsx';
import DeleteOperationWizard from './deleteOperation.jsx';
import { ENTRY_MENU } from '../lib/constants.jsx';

class GenericWizard extends React.Component {
    render () {
        const wizardProps = {
            // Properties
            isWizardOpen: this.props.isWizardOpen,
            wizardEntryDn: this.props.wizardEntryDn,
            editorLdapServer: this.props.editorLdapServer,
            // Functions
            toggleOpenWizard: this.props.toggleOpenWizard,
            setWizardOperationInfo: this.props.setWizardOperationInfo,
            onReload: this.props.onReload,
            onModrdnReload: this.props.onModrdnReload,
            addNotification: this.props.addNotification,
            // LDAP Data
            treeViewRootSuffixes: this.props.treeViewRootSuffixes,
            allObjectclasses: this.props.allObjectclasses,
        };

        switch (this.props.wizardName) {
            case ENTRY_MENU.acis:
                return <AciWizard
                    {...wizardProps }
                />;
            case ENTRY_MENU.new:
                return <NewEntryWizard
                    {...wizardProps }
                />;
            case ENTRY_MENU.edit:
                return <EditLdapEntry
                    {...wizardProps}
                />;
            case ENTRY_MENU.rename:
                return <RenameEntry
                    {...wizardProps}
                />;
            case ENTRY_MENU.cos:
                return <CoSEntryWizard
                    {...wizardProps}
                />;
            case ENTRY_MENU.delete:
                return <DeleteOperationWizard
                    {...wizardProps }
                />;
            default:
                console.log(`Unknown Wizard in GenericWizard class: ${this.props.wizardName}`);
                return null;
        }
    }
}

export default GenericWizard;
