import React from 'react';
import AciWizard from './aci.jsx';
import NewEntryWizard from './newEntry.jsx';
import EditEntryWizard from './editEntry.jsx';
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
        };

        switch (this.props.wizardName) {
            case ENTRY_MENU.acis:
                return <AciWizard
                    {...wizardProps }
                    treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                    />;
            case ENTRY_MENU.new:
                return <NewEntryWizard
                    {...wizardProps }
                    treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                />;
            case ENTRY_MENU.edit:
                return <EditEntryWizard
                    {...wizardProps }
                    allObjectclasses={this.props.allObjectclasses}
                    treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                />;
            case ENTRY_MENU.delete:
                return <DeleteOperationWizard {...wizardProps } />;
            default:
                console.log(`Unknown Wizard in GenericWizard class: ${this.props.wizardName}`);
                return null;
        }
    }
}

export default GenericWizard;
