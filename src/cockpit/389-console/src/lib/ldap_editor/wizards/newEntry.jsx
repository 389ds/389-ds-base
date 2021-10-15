import React from 'react';
import {
  Alert,
  Card, CardTitle, CardBody,
  Radio, Wizard, TextArea, Form, FormGroup,
  Pagination
} from '@patternfly/react-core';
import {
  Table, TableHeader, TableBody, TableVariant, headerCol
} from '@patternfly/react-table';
import {
  ENTRY_TYPE
} from '../lib/constants.jsx';
import AddUser from './operations/addUser.jsx';
import AddLdapEntry from './operations/addLdapEntry.jsx';
import GenericUpdate from './operations/genericUpdate.jsx';

class NewEntryWizard extends React.Component {
  constructor (props) {
    super(props);

    this.state = {
      stepIdReached: 1,
      getStartedStepRadio: 'User'
      /* itemCountAddUser: 0,
      pageAddUser: 1,
      perPageAddUser: 10,
      columnsUser: [
        { title: 'Attribute Name', cellTransforms: [headerCol()] },
        { title: 'From ObjectClass' }
      ],
      rowsUser: [],
      pagedRowsUser: [] */
    };
  }

  handleOnChange = (_, event) => {
    // console.log('event.currentTarget.value = ' + event.currentTarget.value);
    this.setState({ getStartedStepRadio: event.currentTarget.value });
  };

  createInitialLayout=() => {
    // console.log(`this.props.createRootEntry = ${this.props.createRootEntry}`);
    // Creation of a root entry.
    if (this.props.createRootEntry) {
      return ([
        {
          id: 1,
          name: 'Get Started',
          component: (
            <Card isHoverable>
              <CardTitle>Requirement for a root entry creation</CardTitle>
              <CardBody>
                <Alert variant="info" isInline
                  title="The object class must contain the attribute used to name the suffix."
                />
              </CardBody>
              <CardBody>
               For example, if the entry corresponds to the suffix <code>ou=people,dc=example,dc=com</code>,
               then choose the <code>organizationalUnit</code> object class or
               another object class that allows the <code>ou</code> attribute.
              </CardBody>
              <CardBody>
                <Alert variant="default" isInline
                  title={`The root entry to create is "${this.props.entryParentDn}"`}
                >
                  Make sure to select an <strong>ObjectClass</strong> that allows or requires the attribute
                  <strong> {this.props.entryParentDn.split('=')[0]}</strong>
                </ Alert>
              </CardBody>
            </Card>
          )
        }
      ])
    }

    // Creation of a normal (non-root) entry.
    return ([
      {
        id: 1,
        name: 'Get Started',
        component: (
          <div>
            <Radio
              value="User"
              isChecked={this.state.getStartedStepRadio === 'User'}
              onChange={this.handleOnChange}
              label="Create a new User"
              description="Add a new User (inetOrgPerson objectClass)"
              name="radio-new-step-start"
              id="radio-new-step-start-1"
            />
            {/* <Radio
              className="ds-margin-top-lg"
              value="Group"
              isDisabled
              isChecked={this.state.getStartedStepRadio === 'Group'}
              onChange={this.handleOnChange}
              label="Create a new Group"
              description="Add a new Group (Static or Dynamic)"
              name="radio-new-step-start"
              id="radio-new-step-start-2"
            /> */}
            <Radio
              className="ds-margin-top-lg"
              value="OrganizationalUnit"
              isChecked={this.state.getStartedStepRadio === 'OrganizationalUnit'}
              onChange={this.handleOnChange}
              label="Create a new Organizational Unit"
              description="Add a new Organizational Unit"
              name="radio-new-step-start"
              id="radio-new-step-start-3"
            />
            {/* <Radio
              className="ds-margin-top-lg"
              value="Role"
              isDisabled
              isChecked={this.state.getStartedStepRadio === 'Role'}
              onChange={this.handleOnChange}
              label="Create a new Role"
              description="Add a new Role (Filtered / Managed / Nested)"
              name="radio-new-step-start"
              id="radio-new-step-start-4"
            />
            <Radio
              className="ds-margin-top-lg"
              value="CoS"
              isDisabled
              isChecked={this.state.getStartedStepRadio === 'CoS'}
              onChange={this.handleOnChange}
              label="Create a new CoS"
              description="Add a new Class of Service (Classic / Indirect / Pointer)"
              name="radio-new-step-start"
              id="radio-new-step-start-5"
            /> */}
            <Radio
              className="ds-margin-top-lg"
              value="Other"
              isChecked={this.state.getStartedStepRadio === 'Other'}
              onChange={this.handleOnChange}
              label="Create a new custom Entry"
              description="Add a new entry by selecting ObjectClasses and Attributes"
              name="radio-new-step-start"
              id="radio-new-step-start-6"
            />
          </div>
        )
      }]
    )
  }

  render () {
    const {
      getStartedStepRadio
    } = this.state;

    const initialStep = this.createInitialLayout();

    let mySteps;
    let myTitle = '';
    // const typeOfEntry = this.props.createRootEntry ? '' : getStartedStepRadio;

    /* const wizardProps = {
      isOpen: this.props.isWizardOpen,
      onClose: this.props.toggleOpenWizard,
      wizardEntryDn: this.props.wizardEntryDn,
      editorLdapServer: this.props.editorLdapServer
    }; */
    const wizardProps = {
      isWizardOpen: this.props.isWizardOpen,
      toggleOpenWizard: this.props.toggleOpenWizard,
      wizardEntryDn: this.props.wizardEntryDn,
      editorLdapServer: this.props.editorLdapServer,
      setWizardOperationInfo: this.props.setWizardOperationInfo,
      firstStep: initialStep,
      onReload: this.props.onReload
    };

    if (getStartedStepRadio === 'User') {
      return <AddUser
        {...wizardProps}
      />
    } else if (getStartedStepRadio === 'Group') {
      myTitle = 'Add a new Group Entry';
      mySteps = [
      // ...this.state.initialStep,
        { id: 2, name: 'Group 2', component: <p>Step 2</p>, canJumpTo: this.state.stepIdReached >= 2 },
        { id: 3, name: 'Group 3', component: <p>Step 3</p>, canJumpTo: this.state.stepIdReached >= 3 },
        { id: 4, name: 'Group 4', component: <p>Step 4</p>, canJumpTo: this.state.stepIdReached >= 4 },
        { id: 5, name: 'Group End', component: <p>Review Step</p>, nextButtonText: 'Finish', canJumpTo: this.state.stepIdReached >= 5 }
      ];
    } else if (getStartedStepRadio === 'OrganizationalUnit') {
      return <GenericUpdate
              editorLdapServer={this.props.editorLdapServer}
              entryType={ENTRY_TYPE.ou}
              isWizardOpen={this.props.isWizardOpen}
              toggleOpenWizard={this.props.toggleOpenWizard}
              setWizardOperationInfo={this.props.setWizardOperationInfo}
              wizardEntryDn={this.props.wizardEntryDn}
              firstStep={initialStep}
              onReload={this.props.onReload}
          />;
    } else if (getStartedStepRadio === 'CoS') {
      myTitle = 'Add a new Class of Service Entry';
      mySteps = [
      // ...this.state.initialStep,
        { id: 2, name: 'CoS 2', component: <p>Step 2</p>, canJumpTo: this.state.stepIdReached >= 2 },
        { id: 3, name: 'CoS 3', component: <p>Step 3</p>, canJumpTo: this.state.stepIdReached >= 3 },
        { id: 4, name: 'CoS 4', component: <p>Step 4</p>, canJumpTo: this.state.stepIdReached >= 4 },
        { id: 5, name: 'CoS End', component: <p>Review Step</p>, nextButtonText: 'Finish', canJumpTo: this.state.stepIdReached >= 5 }
      ];
    } else if (getStartedStepRadio === 'Role') {
      myTitle = 'Add a new Role Entry';
      mySteps = [
      // ...this.state.initialStep,
        { id: 2, name: 'Role 2', component: <p>Step 2</p>, canJumpTo: this.state.stepIdReached >= 2 },
        { id: 3, name: 'Role 3', component: <p>Step 3</p>, canJumpTo: this.state.stepIdReached >= 3 },
        { id: 4, name: 'Role 4', component: <p>Step 4</p>, canJumpTo: this.state.stepIdReached >= 4 },
        { id: 5, name: 'Role End', component: <p>Review Step</p>, nextButtonText: 'Finish', canJumpTo: this.state.stepIdReached >= 5 }
      ];
    } else {
      return <AddLdapEntry
        {...wizardProps}
      />
    }
  }
}

export default NewEntryWizard;
