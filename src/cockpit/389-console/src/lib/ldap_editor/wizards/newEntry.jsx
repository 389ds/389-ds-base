import cockpit from "cockpit";
import React from 'react';
import {
    Alert,
    Card,
    CardTitle,
    CardBody,
    Radio,
} from '@patternfly/react-core';
import {
    ENTRY_TYPE
} from '../lib/constants.jsx';
import AddUser from './operations/addUser.jsx';
import AddGroup from './operations/addGroup.jsx';
import AddRole from './operations/addRole.jsx';
import AddLdapEntry from './operations/addLdapEntry.jsx';
import GenericUpdate from './operations/genericUpdate.jsx';

const _ = cockpit.gettext;

const ENTRY_TYPE_OPTIONS = [
    { value: 'User', inputId: 'radio-user-step' },
    { value: 'Group', inputId: 'radio-group-step' },
    { value: 'OrganizationalUnit', inputId: 'radio-ou-step' },
    { value: 'Role', inputId: 'radio-role-step' },
    { value: 'Custom', inputId: 'radio-custom-step' },
];

/*
 * Step 1 entry-type radios. Kept as a component so arrow-key changes update
 * selection in place without remounting the parent PatternFly Wizard.
 */
class GetStartedStep extends React.Component {
    focusSelectedRadio = () => {
        const option = ENTRY_TYPE_OPTIONS.find(opt => opt.value === this.props.selected);
        if (!option) {
            return;
        }
        /*
         * Defer until after the operation wizard remounts so focus stays on the
         * radio group instead of the wizard close button.
         */
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

        /*
         * Stop the deprecated PatternFly Wizard from treating arrows as step
         * navigation. Prevent default so the browser does not toggle uncontrolled
         * checked state on inputs we drive via isChecked.
         */
        event.stopPropagation();
        event.preventDefault();

        const { selected, onSelect } = this.props;
        const currentIdx = ENTRY_TYPE_OPTIONS.findIndex(opt => opt.value === selected);
        if (currentIdx === -1) {
            return;
        }

        const step = (event.key === 'ArrowDown' || event.key === 'ArrowRight') ? 1 : -1;
        const nextIdx = (currentIdx + step + ENTRY_TYPE_OPTIONS.length) % ENTRY_TYPE_OPTIONS.length;
        const next = ENTRY_TYPE_OPTIONS[nextIdx];

        onSelect(next.value);
    };

    render () {
        const { selected, onChange } = this.props;

        return (
            <div
                role="radiogroup"
                aria-label={_("New entry type")}
                onKeyDown={this.handleKeyDown}
            >
                <Radio
                    value="User"
                    isChecked={selected === 'User'}
                    onChange={onChange}
                    label={_("Create a new User")}
                    description={_("Add a new User (inetOrgPerson objectClass)")}
                    name="radio-new-step-btn-group"
                    id="radio-user-step"
                />
                <Radio
                    className="ds-margin-top-lg"
                    value="Group"
                    isChecked={selected === 'Group'}
                    onChange={onChange}
                    label={_("Create a new Group")}
                    description={_("Add a new Group (GroupOfNames/GroupOfUniqueNames objectClass)")}
                    name="radio-new-step-btn-group"
                    id="radio-group-step"
                />
                <Radio
                    className="ds-margin-top-lg"
                    value="OrganizationalUnit"
                    isChecked={selected === 'OrganizationalUnit'}
                    onChange={onChange}
                    label={_("Create a new Organizational Unit")}
                    description={_("Add a new Organizational Unit")}
                    name="radio-new-step-btn-group"
                    id="radio-ou-step"
                />
                <Radio
                    className="ds-margin-top-lg"
                    value="Role"
                    isChecked={selected === 'Role'}
                    onChange={onChange}
                    label={_("Create a new Role")}
                    description={_("Add a new Role (Filtered / Managed / Nested)")}
                    name="radio-new-step-btn-group"
                    id="radio-role-step"
                />
                <Radio
                    className="ds-margin-top-lg"
                    value="Custom"
                    isChecked={selected === 'Custom'}
                    onChange={onChange}
                    label={_("Create a new custom Entry")}
                    description={_("Add a new entry by selecting ObjectClasses and Attributes")}
                    name="radio-new-step-btn-group"
                    id="radio-custom-step"
                />
            </div>
        );
    }
}

class NewEntryWizard extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            getStartedStepRadio: 'User',
            /*
             * Which operation wizard is mounted. Stays stable while the user
             * moves through the step-1 radio group so arrow keys do not remount
             * the PatternFly Wizard and steal focus.
             */
            activeWizardType: 'User',
        };
    }

    componentDidUpdate (prevProps) {
        if (this.props.isWizardOpen && !prevProps.isWizardOpen) {
            const defaultType = this.props.createRootEntry ? 'Custom' : 'User';
            this.setState({
                getStartedStepRadio: defaultType,
                activeWizardType: defaultType,
            });
        }
    }

    handleOnChange = (event) => {
        this.setSelectedEntryType(event.currentTarget.value);
    };

    setSelectedEntryType = (value) => {
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

    onToggleWizard = () => {
        this.props.handleToggleWizard();
    };

    createInitialLayout = () => {
        // Creation of a root entry.
        if (this.props.createRootEntry) {
            return ([
                {
                    id: 1,
                    name: _("Get Started"),
                    component: (
                        <Card isSelectable>
                            <CardTitle>{_("Requirement for a root entry creation")}</CardTitle>
                            <CardBody>
                                <Alert
variant="info" isInline
                                    title={_("The object class must contain the attribute used to name the suffix.")}
                                />
                            </CardBody>
                            <CardBody>
                                {_("For example, if the entry corresponds to the suffix <code>ou=people,dc=example,dc=com</code>, then choose the<code>organizationalUnit</code>object class or another object class that allows the <code>ou</code> attribute.")}
                            </CardBody>
                            <CardBody>
                                <Alert
variant="custom" isInline
                                    title={cockpit.format(_("The root entry to create is $0"), this.props.entryParentDn)}
                                >
                                    {_("Make sure to select an <strong>ObjectClass</strong>that allows or requires the attribute")}
                                    <strong> {this.props.entryParentDn.split('=')[0]}</strong>
                                </Alert>
                            </CardBody>
                        </Card>
                    )
                }
            ]);
        }

        // Creation of a normal (non-root) entry.
        return ([
            {
                id: 1,
                name: _("Get Started"),
                component: (
                    <GetStartedStep
                        selected={this.state.getStartedStepRadio}
                        onChange={this.handleOnChange}
                        onSelect={this.setSelectedEntryType}
                    />
                )
            }
        ]);
    };

    render () {
        const {
            activeWizardType,
        } = this.state;

        const initialStep = this.createInitialLayout();

        const wizardProps = {
            isWizardOpen: this.props.isWizardOpen,
            handleToggleWizard: this.props.handleToggleWizard,
            wizardEntryDn: this.props.wizardEntryDn,
            editorLdapServer: this.props.editorLdapServer,
            setWizardOperationInfo: this.props.setWizardOperationInfo,
            firstStep: initialStep,
            onReload: this.props.onReload,
            addNotification: this.props.addNotification,
        };

        if (activeWizardType === 'User') {
            return (
                <AddUser
                    {...wizardProps}
                />
            );
        } else if (activeWizardType === 'Group') {
            return (
                <AddGroup
                    {...wizardProps}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                />
            );
        } else if (activeWizardType === 'Role') {
            return (
                <AddRole
                    {...wizardProps}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                />
            );
        } else if (activeWizardType === 'OrganizationalUnit') {
            return (
                <GenericUpdate
                    editorLdapServer={this.props.editorLdapServer}
                    entryType={ENTRY_TYPE.ou}
                    isWizardOpen={this.props.isWizardOpen}
                    handleToggleWizard={this.onToggleWizard}
                    setWizardOperationInfo={this.props.setWizardOperationInfo}
                    wizardEntryDn={this.props.wizardEntryDn}
                    firstStep={initialStep}
                    onReload={this.props.onReload}
                />
            );
        } else {
            return (
                <AddLdapEntry
                    allObjectclasses={this.props.allObjectclasses}
                    {...wizardProps}
                />
            );
        }
    }
}

export default NewEntryWizard;
