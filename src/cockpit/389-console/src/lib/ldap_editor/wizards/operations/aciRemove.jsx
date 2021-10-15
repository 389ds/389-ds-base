import React from 'react';
import {
  Wizard, Alert, TreeView, TreeViewDataItem
} from '@patternfly/react-core';
import FolderIcon from '@patternfly/react-icons/dist/js/icons/folder-icon';
import FolderOpenIcon from '@patternfly/react-icons/dist/js/icons/folder-open-icon';

class RemoveAci extends React.Component {
  constructor (props) {
    super(props);

    this.state = {

    };

    this.state = { activeItems: {} };

    this.onClick = (evt, treeViewItem, parentItem) => {
      this.setState({
        activeItems: [treeViewItem, parentItem]
      });
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
    const { activeItems } = this.state;
    const options = [
      {
        name: 'ApplicationLauncher',
        id: 'AppLaunch',
        children: [
          {
            name: 'Application 1',
            id: 'App1',
            children: [
              { name: 'Settings', id: 'App1Settings' },
              { name: 'Current', id: 'App1Current' }
            ]
          },
          {
            name: 'Application 2',
            id: 'App2',
            children: [
              { name: 'Settings', id: 'App2Settings' },
              {
                name: 'Loader',
                id: 'App2Loader',
                children: [
                  { name: 'Loading App 1', id: 'LoadApp1' },
                  { name: 'Loading App 2', id: 'LoadApp2' },
                  { name: 'Loading App 3', id: 'LoadApp3' }
                ]
              }
            ]
          }
        ]// ,
        // defaultExpanded: true
      },
      {
        name: 'Cost Management',
        id: 'Cost',
        children: [
          {
            name: 'Application 3',
            id: 'App3',
            children: [
              { name: 'Settings', id: 'App3Settings' },
              { name: 'Current', id: 'App3Current' }
            ]
          }
        ]
      },
      {
        name: 'Sources',
        id: 'Sources',
        children: [{ name: 'Application 4', id: 'App4', children: [{ name: 'Settings', id: 'App4Settings' }] }]
      },
      {
        name: 'Really really really long folder name that overflows the container it is in',
        id: 'Long',
        children: [{ name: 'Application 5', id: 'App5' }]
      }
    ];

    const usersStepComponent = (
      <>
        <Alert variant="danger" isInline title="Someone is trying to remove an ACI!!!" />

        <TreeView
          data={options}
          activeItems={activeItems}
          onSelect={this.onClick}
          // icon={<FolderIcon />}
          // expandedIcon={<FolderOpenIcon />}
        />
      </>
    );

    const newAciSteps = [
      {
        id: 2,
        name: 'Remove ACI',
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

export default RemoveAci;
