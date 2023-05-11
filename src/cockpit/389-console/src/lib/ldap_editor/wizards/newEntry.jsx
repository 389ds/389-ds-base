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

class NewEntryWizard extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            stepIdReached: 1,
            getStartedStepRadio: 'User'
        };
    }

    handleOnChange = (_, event) => {
        // console.log('event.currentTarget.value = ' + event.currentTarget.value);
        this.setState({ getStartedStepRadio: event.currentTarget.value });
    };

    createInitialLayout = () => {
        // console.log(`this.props.createRootEntry = ${this.props.createRootEntry}`);
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
variant="default" isInline
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
                    <div>
                        <Radio
                            value="User"
                            isChecked={this.state.getStartedStepRadio === 'User'}
                            onChange={this.handleOnChange}
                            label={_("Create a new User")}
                            description={_("Add a new User (inetOrgPerson objectClass)")}
                            name="radio-new-step-start"
                            id="radio-new-step-start-1"
                        />
                        <Radio
                            className="ds-margin-top-lg"
                            value="Group"
                            isChecked={this.state.getStartedStepRadio === 'Group'}
                            onChange={this.handleOnChange}
                            label={_("Create a new Group")}
                            description={_("Add a new Group (GroupOfNames/GroupOfUniqueNames objectClass)")}
                            name="group"
                            id="radio-new-step-start-2"
                        />
                        <Radio
                            className="ds-margin-top-lg"
                            value="OrganizationalUnit"
                            isChecked={this.state.getStartedStepRadio === 'OrganizationalUnit'}
                            onChange={this.handleOnChange}
                            label={_("Create a new Organizational Unit")}
                            description={_("Add a new Organizational Unit")}
                            name="radio-new-step-start"
                            id="radio-new-step-start-3"
                        />
                        <Radio
                          className="ds-margin-top-lg"
                          value="Role"
                          isChecked={this.state.getStartedStepRadio === 'Role'}
                          onChange={this.handleOnChange}
                          label={_("Create a new Role")}
                          description={_("Add a new Role (Filtered / Managed / Nested)")}
                          name="radio-new-step-start"
                          id="radio-new-step-start-4"
                        />
                        <Radio
                            className="ds-margin-top-lg"
                            value="Other"
                            isChecked={this.state.getStartedStepRadio === 'Other'}
                            onChange={this.handleOnChange}
                            label={_("Create a new custom Entry")}
                            description={_("Add a new entry by selecting ObjectClasses and Attributes")}
                            name="radio-new-step-start"
                            id="radio-new-step-start-7"
                        />
                    </div>
                )
            }
        ]);
    };

    onToggleWizard () {
        this.props.handleToggleWizard();
    }

    render () {
        const {
            getStartedStepRadio
        } = this.state;

        const initialStep = this.createInitialLayout();

        const wizardProps = {
            isWizardOpen: this.props.isWizardOpen,
            handleToggleWizard: this.props.handleToggleWizard,
            wizardEntryDn: this.props.wizardEntryDn,
            editorLdapServer: this.props.editorLdapServer,
            setWizardOperationInfo: this.props.setWizardOperationInfo,
            firstStep: initialStep,
            onReload: this.props.onReload
        };

        if (getStartedStepRadio === 'User') {
            return (
                <AddUser
                    {...wizardProps}
                />
            );
        } else if (getStartedStepRadio === 'Group') {
            return (
                <AddGroup
                    {...wizardProps}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                />
            );
        } else if (getStartedStepRadio === 'Role') {
            return (
                <AddRole
                    {...wizardProps}
                treeViewRootSuffixes={this.props.treeViewRootSuffixes}
                />
            );
        } else if (getStartedStepRadio === 'OrganizationalUnit') {
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
