import React from 'react';
import {
  Card, CardBody, Grid, GridItem,
  Form, FormGroup, TextArea,
  Wizard
} from '@patternfly/react-core';

import {
  Table, TableHeader, TableBody
} from '@patternfly/react-table';

import CheckCircleIcon from '@patternfly/react-icons/dist/js/icons/check-circle-icon';
import CheckIcon from '@patternfly/react-icons/dist/js/icons/check-icon';
import ExclamationCircleIcon from '@patternfly/react-icons/dist/js/icons/exclamation-circle-icon';
import TimesIcon from '@patternfly/react-icons/dist/js/icons/times-icon';
import ExclamationTriangleIcon from '@patternfly/react-icons/dist/js/icons/exclamation-triangle-icon';

import {
  getAciAttributes, getAciActualName
} from '../../lib/aciParser.jsx';

class AciManualEdition extends React.Component {
  constructor (props) {
    super(props);
    this.state = {
      stepIdReachedManual: 2,
      savedStepId: 2,
      value: 'WIP ==> INTENTIONAL FAKE ACI. "Validate ACI" button is disabled on purpose' +
      '(targetattr="member")(version 3.0; acl "Allow users to add/remove themselves from example group";' +
      'allow (selfwrite) userdn = "ldap:///all")',
      invalidText: 'The Access Control Instruction is not valid!',
      // validated: 'warning',
      validated: 'default', // TODO: For demo!
      // helperText: '(target_rule)(version 3.0; acl "ACL_name"; permission_rule bind_rules;)'
      // helperText: 'Empty value for the Access Control Instruction.',
      helperText: 'Value for the Access Control Instruction.', // TODO: For demo!
      isAciParsed: false
    };

    this.validateAfterSomeDelay = callback => {
      setTimeout(callback, 1000);
    }

    this.handleTextAreaChange = value => {
      this.setState({
        value,
        validated: 'default',
        helperText: 'Validating...',
        isAciParsed: false
      },
      this.validateAfterSomeDelay(() => {
        if (value && value.length > 22) { // There should be at least '(version 3.0; acl ""' and the ending ';)'
          if (value.trim().slice(-2) === ';)') { // ';)' indicates the end of an ACI.
            const aciData = getAciAttributes(value);
            aciData.map(datum => {
              console.log(datum);
            });
            if (aciData.length > 0) {
              this.setState({
                isAciParsed: true,
                validated: 'default',
                helperText: 'Please validate the ACI.'
              });
            } else {
              this.setState({
                validated: 'error',
                invalidText: 'The ACI could not be parsed.'
              });
            }
          } else {
            this.setState({
              validated: 'error',
              invalidText: 'Incomplete ACI. An ACI should end with ";)".'
            });
          }
        } else {
          this.setState({
            validated: 'warning',
            helperText: 'Empty value for the Access Control Instruction.'
          });
        }
      }));
    };

    this.onNextManual = ({ id }) => {
      this.setState({
        stepIdReachedManual: this.state.stepIdReachedManual < id ? id : this.state.stepIdReachedManual,
        savedStepId: id
      });
    };

    this.onBackManual = ({ id }) => {
      this.setState({ savedStepId: id });
    };

    // End contructor().
  }

  render () {
    const {
      stepIdReachedManual, value, validated, helperText, invalidText, isAciParsed,
      savedStepId
    } = this.state;

    const columns = ['Component', 'Required', 'Present', 'Valid'];
    const iconReqYes = <CheckIcon />;
    const iconReqNo = <TimesIcon />;
    const iconSuccess = <CheckIcon color="var(--pf-global--success-color--100)" />;
    const iconError = <ExclamationCircleIcon color="var(--pf-global--danger-color--100)" />;
    const iconWarning = <ExclamationTriangleIcon color="var(--pf-global--warning-color--100)" />;
    const iconCheckInfo = <CheckIcon color="var(--pf-global--info-color--100)" />;
    const iconTimesInfo = <TimesIcon color="var(--pf-global--info-color--100)" />;
    const rows = [
      ['Users', { title: iconReqYes }, { title: iconCheckInfo }, { title: iconSuccess }],
      ['Rights', { title: iconReqYes }, { title: iconCheckInfo }, { title: iconWarning }],
      ['Targets', { title: iconReqNo }, { title: iconTimesInfo }, { title: iconSuccess }],
      ['Hosts', { title: iconReqNo }, { title: iconCheckInfo }, { title: iconError }],
      ['Times', { title: iconReqNo }, { title: iconTimesInfo }, { title: iconSuccess }]
    ];

    const editStep = (
      <Form autoComplete="off">
        <FormGroup
          label="Access Control Instruction:"
          type="string"
          helperText={helperText}
          helperTextInvalid={invalidText}
          fieldId="selection"
          validated={validated}
        >
          <TextArea
            value={value}
            onChange={this.handleTextAreaChange}
            isRequired
            validated={validated}
            aria-label="ACI manual edit text area"
            rows={3}
            // cols={5}
            style={{ maxWidth: '75%' }} // To prevent the text area to be expanded outside the container.
          />
        </FormGroup>
      </Form>
    );

    const newAciStepsManual =
       [{
         id: 2,
         name: 'Manual Edit',
         component: (
           <>
             {this.props.aciNameComponent}
             <div className="ds-margin-bottom-md" />
             {editStep}
           </>
         ),
         canJumpTo: stepIdReachedManual >= 2,
         nextButtonText: 'Validate ACI',
         // TODO: To enable!!! Commenting just to speed up testing.
         // enableNext: isAciParsed
         enableNext: false
       },
       {
         id: 3,
         name: 'Validation',
         component: (
           <>
             {this.props.aciNameComponent}
             <div className="ds-margin-bottom-md" />
             <Table
               aria-label="Simple Table"
               variant="compact"
               cells={columns}
               rows={rows}
             >
               <TableHeader />
               <TableBody />
             </Table>
           </>
         ),
         canJumpTo: stepIdReachedManual >= 3,
         nextButtonText: 'Add ACI'
         // enableNext: isAciSyntaxValid
       },
       {
         id: 4,
         name: 'Review',
         component: iconSuccess,
         nextButtonText: 'Close',
         canJumpTo: stepIdReachedManual >= 4
       }];

    return (
      <Wizard
        isOpen={this.props.isWizardOpen}
        onClose={this.props.toggleOpenWizard}
        onNext={this.onNextManual}
        onBack={this.onBackManual}
        title="Add a new Access Control Instruction"
        description={`Entry DN: ${this.props.wizardEntryDn}`}
        steps={[...this.props.firstStep, ...newAciStepsManual]}
        startAtStep={savedStepId}
      />
    );
  }
}

export default AciManualEdition;
