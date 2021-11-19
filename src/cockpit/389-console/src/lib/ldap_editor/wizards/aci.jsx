import React from 'react';
import {
  Alert,
  Label, Radio, Wizard, TextContent,
  Text, TextVariants, TextList, TextListVariants, TextListItem,
  TextListItemVariants, Pagination, SimpleList, SimpleListItem, SimpleListGroup,
  Select, SelectOption, SelectVariant, SelectGroup, Divider
} from '@patternfly/react-core';

import InfoCircleIcon from '@patternfly/react-icons/dist/js/icons/info-circle-icon';

import {
  retrieveAllAcis
} from '../lib/utils.jsx';

import {
  getAciActualName, checkAcis
} from '../lib/aciParser.jsx';

import AddNewAci from './operations/aciNew.jsx';
import EditAci from './operations/aciEdit.jsx';
import RemoveAci from './operations/aciRemove.jsx';

class AciWizard extends React.Component {
  constructor (props) {
    super(props);

    this.state = {
      stepIdReached: 1,
      getStartedStepRadio: 'New',
      ownAciGroup: [],
      inheritedAciGroup: [],
      acisLoaded: false,
      isOpen: false,
      selected: null,
      isOwnAci: false,
      aciSelectionOptions: null // <React.Fragment />
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
      // console.log(`selection = ${selection}`);
      this.setState({
        selected: selection,
        isOpen: false,
        // Own ACIs have the entryDn set to null.
        isOwnAci: selection.entryDn === null
        /* this.state.selected === null
          ? false
          : this.state.selected.entryDn === null */
      });
    };
  } // End constructor

  componentDidMount () {
    const params = {
      serverId: this.props.editorLdapServer,
      baseDn: this.props.wizardEntryDn
    };
    // console.log(params);
    retrieveAllAcis(params, (resultArray) => {
      // console.log('HERE 111111');
      /* resultArray.map(res => {
        console.log(`MY ACIs ==> ${res}`);
      }); */
      // const myOwnAcis = resultArray.filter(aciObj => !aciObj.inherited);
      // const myInheritedAcis = resultArray.filter(aciObj => aciObj.inherited);
      // const ownGroup = this.generateAciGroup(myOwnAcis, false);
      // const inheritedGroup = this.generateAciGroup(myInheritedAcis, true);
      const aciObjectArray = this.generateAciOptionsArray(resultArray);
      const ownAcis = (
        <SelectGroup label="Own ACIs" key="own">
          {aciObjectArray
            .filter(aciObj => !aciObj.inherited)
            .map(aciObj => aciObj.aciSelectOption)
            /* .map((aciObj) => (
              <SelectOption
                key={`key_${aciObj.id}`}
                value={
                  {
                    fullAci: aciObj.fullAci,
                    entryDn: aciObj.entryDn,
                    toString: () => {
                      return aciObj.aciName
                    }
                  }
                }
              />
            )) */
          }
        </SelectGroup>
      );
      const inheritedAcis = (
        <SelectGroup label="Inherited ACIs" key="inherited">
          {aciObjectArray
            .filter(aciObj => aciObj.inherited)
            .map(aciObj => aciObj.aciSelectOption)
            /* .map((aciObj) => (
              <SelectOption
                key={`key_${aciObj.id}`}
                value={
                  {
                    fullAci: aciObj.fullAci,
                    entryDn: aciObj.entryDn,
                    toString: () => {
                      return aciObj.aciName
                    }
                  }
                }
              />
            )) */
          }
        </SelectGroup>
      );

      const aciSelectionOptions = [
        ownAcis,
        <Divider key="divider" />,
        inheritedAcis
      ];

      const acisLoaded = true;
      // console.log('HERE 2222222');
      this.setState({
        aciSelectionOptions,
        acisLoaded
      });
    });
  }

  generateAciOptionsArray = (aciDataArray) => {
    const groupArray = [];
    let index = 0;
    aciDataArray.map(aciObj => {
      const myAciArray = aciObj.aciArray;
      for (const anAci of myAciArray) {
        // TODO: Use spread operator.
        const aciName = getAciActualName(anAci);
        checkAcis(anAci); // TODO: To remove
        const myAciObject = {
          // entryDn: aciObj.entryDn,
          // aciName: getAciActualName(anAci),
          // fullAci: anAci,
          inherited: aciObj.inherited,
          aciSelectOption: (
            <SelectOption
              key={`key_${index}`}
              value={
                {
                  fullAci: anAci,
                  entryDn: aciObj.inherited ? aciObj.entryDn : null,
                  toString: () => { return aciName; }
                }
              }
            />
          )
        };
        groupArray.push(myAciObject);
        index++;
      }
    });
    return groupArray;
  }

  createInitialLayout=() => {
    return ([
      {
        id: 1,
        name: 'Get Started',
        enableNext: this.state.acisLoaded &&
         ((this.state.getStartedStepRadio === 'New') ||
         this.state.isOwnAci), // Only own ACIs can be altered.
        // (this.state.selected.entryDn === null)), // Only own ACIs can be altered. Own ACIs have the entryDn set to null.
        // (this.state.ownAciGroup.length > 0)), // Only own ACIs can be altered.
        component: (
          <React.Fragment>
            <Radio
              value="New"
              isChecked={this.state.getStartedStepRadio === 'New'}
              onChange={this.handleOnChange}
              label="Create a new ACI"
              description="Create a new ACI which will be stored in this entry."
              name="radio-aci-step-start"
              id="radio-aci-step-start-1"
            />
            <Radio
              value="Edit"
              className="ds-margin-top-lg"
              isChecked={this.state.getStartedStepRadio === 'Edit'}
              onChange={this.handleOnChange}
              label="Edit an existing ACI"
              description="Edit an ACI stored in this entry. Inherited ACIs cannot be edited."
              name="radio-aci-step-start"
              id="radio-aci-step-start-2"
              isDisabled // WIP
            />
            <Radio
              value="Remove"
              className="ds-margin-top-lg"
              isChecked={this.state.getStartedStepRadio === 'Remove'}
              onChange={this.handleOnChange}
              label="Remove an existing ACI"
              description="Delete an ACI stored in this entry. Inherited ACIs cannot be deleted."
              name="radio-aci-step-start"
              id="radio-aci-step-start-3"
              isDisabled // WIP
            />

            {(this.state.getStartedStepRadio !== 'New') &&
              <React.Fragment>
                <div className="ds-margin-bottom-md" />
                <Divider inset={{ default: 'insetMd' }} />
                <div className="ds-margin-bottom-md" />

                <Select
                  variant={SelectVariant.single}
                  onToggle={this.onToggle}
                  onSelect={this.onSelect}
                  selections={this.state.selected}
                  isOpen={this.state.isOpen}
                  placeholderText={`Select the ACI to
                    ${this.state.getStartedStepRadio === 'Edit'
                      ? 'edit'
                      : 'remove'}`}
                  // aria-labelledby={titleId}
                  isGrouped
                >
                  {this.state.aciSelectionOptions}
                </Select>

                {(this.state.selected !== null) &&
                  <React.Fragment>
                    <div className="ds-margin-bottom-md" />
                    {
                      (this.state.selected.entryDn !== null) &&
                      <Label variant="outline" color="red" icon={<InfoCircleIcon />}>
                        Inherited ACIs cannot be edited or removed.
                      </Label>
                    }
                    <div className="ds-margin-bottom-md" />
                    <TextContent>
                      <TextList component={TextListVariants.dl}>
                        {(this.state.selected.entryDn !== null) &&
                        <>
                          <TextListItem component={TextListItemVariants.dt}>Stored in</TextListItem>
                          <TextListItem component={TextListItemVariants.dd}>{this.state.selected.entryDn}</TextListItem>
                        </>
                        }
                        <TextListItem component={TextListItemVariants.dt}>Full ACI</TextListItem>
                        <TextListItem component={TextListItemVariants.dd}>{this.state.selected.fullAci}</TextListItem>
                      </TextList>
                    </TextContent>
                  </React.Fragment>
                }
              </React.Fragment>
            }

          </React.Fragment>
        )
      }]
    )
  }

  render () {
    const {
      getStartedStepRadio
    } = this.state;

    // console.log(`this.state.acisLoaded = ${this.state.acisLoaded}`);
    // console.log(`this.state.isOwnAci = ${this.state.isOwnAci}`);

    const initialStep = this.createInitialLayout();

    const wizardProps = {
      isWizardOpen: this.props.isWizardOpen,
      toggleOpenWizard: this.props.toggleOpenWizard,
      wizardEntryDn: this.props.wizardEntryDn,
      editorLdapServer: this.props.editorLdapServer
    };

    /* isOpen={this.props.isWizardOpen}
    onClose={this.props.toggleOpenWizard}
    onNext={this.onNext}
    title="Add a new Access Control Instruction"
    description={`Entry DN: ${this.props.wizardEntryDn}`}
    steps={[...this.props.firstStep, ...addUserSteps]} */

    if (getStartedStepRadio === 'New') {
      return <AddNewAci
        {...wizardProps}
        firstStep={initialStep}
        treeViewRootSuffixes={this.props.treeViewRootSuffixes}
      />
    } else if (getStartedStepRadio === 'Edit') {
      return <EditAci
        {...wizardProps}
        firstStep={initialStep}
      />
    } else { // getStartedStepRadio is equal to 'Remove'
      return <RemoveAci
        {...wizardProps}
        firstStep={initialStep}
      />
    }
  }
}

export default AciWizard;
