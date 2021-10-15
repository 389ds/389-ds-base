import React from 'react';
import {
  TimePicker,
  Wizard, DualListSelector
} from '@patternfly/react-core';

class EditAci extends React.Component {
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

    this.timeOnChange = (time) => {
      console.log(time);
    };
    // End constructor().
  }

  componentDidMount () {

  }

  render () {
    const usersStepComponent = (
      <TimePicker defaultTime="2021-10-14T19:10:02Z" is24Hour onChange={this.timeOnChange}/>
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
        title="Add a new Access Control Instruction"
        description={`Entry DN: ${this.props.wizardEntryDn}`}
        steps={[...this.props.firstStep, ...newAciSteps]}
      />
    );
  }
}

export default EditAci;
