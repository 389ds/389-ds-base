import React from 'react';
import {
    DualListSelector,
    TimePicker,
    Wizard,
} from '@patternfly/react-core';
import EditableTable from '../../lib/editableTable.jsx';

class EditEntry extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            stepIdReached: 1
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });

            if (id === 3) {

            }
        };
        // End constructor().
    }

    componentDidMount () {

    }

    render () {
        const usersStepComponent = (
            <TimePicker defaultTime="2021-10-14T19:10:02Z" is24Hour />
        );

        const newEditSteps = [
            {
                id: 2,
                name: 'Edit ACI',
                component: usersStepComponent,
                canJumpTo: this.state.stepIdReached >= 2
            },
            {
                id: 3,
                name: 'Set Values',
                component: <EditableTable></EditableTable>,
                canJumpTo: this.state.stepIdReached >= 3
            }/*,
            {
                id: 4,
                name: 'Create User',
                component: null,
                nextButtonText: 'Create',
                canJumpTo: this.state.stepIdReached >= 4
            },
            {
                id: 5,
                name: 'Review Result',
                component: null,
                nextButtonText: 'Finish',
                canJumpTo: this.state.stepIdReached >= 5
            } */
        ];

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                onNext={this.onNext}
                title="Edit LDAP Entry"
                description={`Entry DN: ${this.props.wizardEntryDn}`}
                steps={[...this.props.firstStep, ...newEditSteps]}
            />
        );
    }
}

export default EditEntry;
