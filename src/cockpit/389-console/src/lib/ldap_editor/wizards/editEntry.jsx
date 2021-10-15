import React from 'react';
import {
    Alert,
    Card, CardBody, CardTitle,
    Divider,
    DualListSelector,
    Pagination,
    Radio,
    SimpleList, SimpleListItem, SimpleListGroup,
    Select, SelectOption, SelectVariant, SelectGroup,
    TextContent, Text, TextVariants,
    TextList, TextListVariants, TextListItem, TextListItemVariants,
    Wizard,
} from '@patternfly/react-core';
import {
    retrieveAllAcis
} from '../lib/utils.jsx';
import EditEntry from './operations/edit.jsx';
import UpdateEntry from './operations/update.jsx';
import RenameEntry from './operations/rename.jsx';

class EditEntryWizard extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            stepIdReached: 1,
            getStartedStepRadio: 'Update',
            isOpen: false,
            selected: null
        };

        this.handleOnChange = (_, event) => {
            this.setState({ getStartedStepRadio: event.currentTarget.value });
        };

        this.onToggle = isOpen => {
            this.setState({
                isOpen
            });
        };

        this.onSelect = (event, selection) => {
            this.setState({
                selected: selection,
                isOpen: false
            });
        };
    } // End constructor

    componentDidMount () {

    }

    createInitialLayout=() => {
        return ([
            {
                id: 1,
                name: 'Get Started',
                component: (
                    <React.Fragment>
                        <Radio
                            value="Update"
                            isChecked={this.state.getStartedStepRadio === 'Update'}
                            onChange={this.handleOnChange}
                            label="Update Value(s)"
                            description="Update existing values."
                            name="radio-edit-step-start"
                            id="radio-edit-step-update"
                        />
                        <Radio
                            value="Edit"
                            className="ds-margin-top-lg"
                            isChecked={this.state.getStartedStepRadio === 'Edit'}
                            isDisabled
                            onChange={this.handleOnChange}
                            label="Edit Entry"
                            description="Add, modify or remove object classes and attributes."
                            name="radio-edit-step-start"
                            id="radio-edit-step-edit"
                        />
                        <Radio
                            value="Rename"
                            className="ds-margin-top-lg"
                            isChecked={this.state.getStartedStepRadio === 'Rename'}
                            isDisabled
                            onChange={this.handleOnChange}
                            label="Rename Entry"
                            description="Rename an LDAP entry by changing relative distinguished name."
                            name="radio-edit-step-start"
                            id="radio-edit-step-rename"
                        />
                    </React.Fragment>
                )
            }]
        )
    }

    render () {
        const {
            getStartedStepRadio
        } = this.state;

        const initialStep = this.createInitialLayout();
        const wizardProps = {
            isWizardOpen: this.props.isWizardOpen,
            toggleOpenWizard: this.props.toggleOpenWizard,
            wizardEntryDn: this.props.wizardEntryDn,
            editorLdapServer: this.props.editorLdapServer,
            setWizardOperationInfo: this.props.setWizardOperationInfo
        };

        if (getStartedStepRadio === 'Update') {
            return <UpdateEntry
                {...wizardProps}
                firstStep={initialStep}
                allObjectclasses={this.props.allObjectclasses}
            />
        } else if (getStartedStepRadio === 'Edit') {
            return <EditEntry
                {...wizardProps}
                firstStep={initialStep}
            />
        } else { // Renaming.
            return <RenameEntry
                {...wizardProps}
                firstStep={initialStep}
            />
        }
    }
}

export default EditEntryWizard;
