import React from 'react';
import {
  TimePicker,
  Wizard, DualListSelector
} from '@patternfly/react-core';

class RenameEntry extends React.Component {
  constructor (props) {
    super(props);

    this.state = {

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
      <TimePicker defaultTime="2030-10-14T19:10:02Z" />
    );

    const newAciSteps = [
      {
        id: 2,
        name: 'Edit ACI',
        component: usersStepComponent,
        canJumpTo: this.state.stepIdReached >= 2
      }/*,
      {
        id: 3,
        name: 'Set Values',
        component: null,
        canJumpTo: this.state.stepIdReached >= 3,
        enableNext: noEmptyValue
      },
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
        title="Rename LDAP Entry"
        description={`Entry Current DN: ${this.props.wizardEntryDn}`}
        steps={[...this.props.firstStep, ...newAciSteps]}
      />
    );
  }
}

export default RenameEntry;
