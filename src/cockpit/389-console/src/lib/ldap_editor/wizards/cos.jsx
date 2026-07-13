import cockpit from "cockpit";
import React from 'react';
import {
    Grid,
    GridItem,
    Radio,
    Text,
    TextContent,
    TextVariants,
} from '@patternfly/react-core';
import AddCosDefinition from './operations/addCosDefinition.jsx';
import AddCosTemplate from './operations/addCosTemplate.jsx';

const _ = cockpit.gettext;

const COS_TYPE_OPTIONS = [
    { value: 'CoSDefinition', inputId: 'radio-cos-def-step' },
    { value: 'CoSTemplate', inputId: 'radio-cos-template-step' },
];

/*
 * Step 1 CoS-type radios. Kept as a component so arrow-key changes update
 * selection and sidebar steps without losing focus on the radio group.
 */
class CoSGetStartedStep extends React.Component {
    focusSelectedRadio = () => {
        const option = COS_TYPE_OPTIONS.find(opt => opt.value === this.props.selected);
        if (!option) {
            return;
        }
        requestAnimationFrame(() => {
            document.getElementById(option.inputId)?.focus();
        });
    };

    componentDidMount () {
        this.focusSelectedRadio();
    }

    componentDidUpdate (prevProps) {
        if (prevProps.selected !== this.props.selected) {
            this.focusSelectedRadio();
        }
    }

    handleKeyDown = (event) => {
        const isArrow = event.key === 'ArrowUp' || event.key === 'ArrowDown' ||
            event.key === 'ArrowLeft' || event.key === 'ArrowRight';
        if (!isArrow) {
            return;
        }

        event.stopPropagation();
        event.preventDefault();

        const { selected, onSelect } = this.props;
        const currentIdx = COS_TYPE_OPTIONS.findIndex(opt => opt.value === selected);
        if (currentIdx === -1) {
            return;
        }

        const step = (event.key === 'ArrowDown' || event.key === 'ArrowRight') ? 1 : -1;
        const nextIdx = (currentIdx + step + COS_TYPE_OPTIONS.length) % COS_TYPE_OPTIONS.length;
        const next = COS_TYPE_OPTIONS[nextIdx];

        onSelect(next.value);
    };

    render () {
        const { selected, onChange } = this.props;

        return (
            <div
                role="radiogroup"
                aria-label={_("CoS type")}
                onKeyDown={this.handleKeyDown}
            >
                <Radio
                    className="ds-margin-top-lg"
                    value="CoSDefinition"
                    isChecked={selected === 'CoSDefinition'}
                    onChange={onChange}
                    label={_("Create a new CoS Definition")}
                    description={_("The CoS definition entry identifies the type of CoS used. The CoS definition entry is below the branch at which it is effective.")}
                    name="radio-cos-step-btn-group"
                    id="radio-cos-def-step"
                />
                <Radio
                    className="ds-margin-top-lg"
                    value="CoSTemplate"
                    isChecked={selected === 'CoSTemplate'}
                    onChange={onChange}
                    label={_("Create a new CoS Template")}
                    description={_("The CoS template entry contains a list of the shared attribute values. Changes to the template entry attribute values are automatically applied to all the entries within the scope of the CoS.")}
                    name="radio-cos-step-btn-group"
                    id="radio-cos-template-step"
                />
            </div>
        );
    }
}

class CoSEntryWizard extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            getStartedStepRadio: 'CoSDefinition',
            activeWizardType: 'CoSDefinition',
        };
    }

    componentDidUpdate (prevProps) {
        if (this.props.isWizardOpen && !prevProps.isWizardOpen) {
            this.setState({
                getStartedStepRadio: 'CoSDefinition',
                activeWizardType: 'CoSDefinition',
            });
        }
    }

    handleOnChange = (event) => {
        this.setSelectedCosType(event.currentTarget.value);
    };

    setSelectedCosType = (value) => {
        this.setState(prevState => {
            if (prevState.getStartedStepRadio === value &&
                prevState.activeWizardType === value) {
                return null;
            }
            return {
                getStartedStepRadio: value,
                activeWizardType: value,
            };
        });
    };

    createInitialLayout = () => {
        return ([
            {
                id: 1,
                name: _("Get Started"),
                component: (
                    <div>
                        <Grid>
                            <GridItem span={12}>
                                <TextContent>
                                    <Text component={TextVariants.h3}>{_("Select CoS Type")}</Text>
                                </TextContent>
                            </GridItem>
                            <GridItem span={12}>
                                <TextContent className="ds-margin-top">
                                    <Text>
                                        {_("A class of service definition (CoS) shares attributes between entries in a way that is transparent to applications. CoS simplifies entry management and reduces storage requirements.")}
                                    </Text>
                                    <Text>
                                        {_("Clients of the Directory Server read the attributes in a user's entry. With CoS, some attribute values may not be stored within the entry itself. Instead, these attribute values are generated by class of service logic as the entry is sent to the client application.")}
                                    </Text>
                                </TextContent>
                            </GridItem>
                        </Grid>
                        <CoSGetStartedStep
                            selected={this.state.getStartedStepRadio}
                            onChange={this.handleOnChange}
                            onSelect={this.setSelectedCosType}
                        />
                    </div>
                )
            }
        ]);
    };

    render () {
        const { activeWizardType } = this.state;

        const initialStep = this.createInitialLayout();

        const wizardProps = {
            isWizardOpen: this.props.isWizardOpen,
            handleToggleWizard: this.props.handleToggleWizard,
            wizardEntryDn: this.props.wizardEntryDn,
            editorLdapServer: this.props.editorLdapServer,
            setWizardOperationInfo: this.props.setWizardOperationInfo,
            firstStep: initialStep,
            onReload: this.props.onReload,
            addNotification: this.props.addNotification
        };

        if (activeWizardType === 'CoSDefinition') {
            return (
                <AddCosDefinition
                    {...wizardProps}
                    allObjectclasses={this.props.allObjectclasses}
                    treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                    createdTemplateDN=""
                    stepReached={1}
                    cosDefName=""
                    cosDefDesc=""
                    cosDefType="pointer"
                />
            );
        }

        return (
            <AddCosTemplate
                {...wizardProps}
                allObjectclasses={this.props.allObjectclasses}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                definitionWizardEntryDn=""
                stepReached={1}
            />
        );
    }
}

export default CoSEntryWizard;
