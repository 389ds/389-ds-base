import cockpit from "cockpit";
import React from 'react';
import {
    Alert,
    Card,
    CardBody,
    CardTitle,
    ClipboardCopy,
    ClipboardCopyVariant,
    Spinner,
    Switch,
    Wizard
} from '@patternfly/react-core';
import {
    headerCol,
} from '@patternfly/react-table';
import {
    UserIcon,
    UsersIcon,
} from '@patternfly/react-icons';
import {
    getBaseLevelEntryFullAttributes, deleteLdapData
} from '../lib/utils.jsx';

const _ = cockpit.gettext;

class DeleteOperationWizard extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            alertVariant: 'danger',
            entryLdifData: '',
            numSubordinates: -1,
            ldapsearchCmd: '',
            isAckChecked: false,
            isDelChecked: false,
            commandOutput: '',
            resultVariant: 'default',
            allAttributesSelected: false,
            stepIdReached: 1,
            itemCountAddUser: 0,
            pageAddUser: 1,
            perPageAddUser: 10,
            columnsUser: [
                { title: _("Attribute Name"), cellTransforms: [headerCol()] },
                { title: _("From ObjectClass") }
            ],
            rowsUser: [],
            pagedRowsUser: [],
            selectedAttributes: ['cn', 'sn'],
            // Values
            noEmptyValue: false,
            columnsValue: [
                'Attribute',
                'Value'
            ],
            rowsValues: [],
            // Review step
            reviewValue: '',
            reviewInvalidText: _("Invalid LDIF"),
            reviewIsValid: true,
            reviewValidated: 'default',
            // reviewHelperText: 'LDIF data',
            deleting: true,
        };

        this.handleNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });

            // Actual deletion step.
            if (id === 4) {
                deleteLdapData(this.props.editorLdapServer,
                               this.props.wizardEntryDn,
                               this.state.numSubordinates,
                               (result) => {
                                   this.setState({
                                       commandOutput: result.errorCode === 0 ? _("Successfully deleted entry") : _("Failed to delete entry, error: ") + result.errorCode,
                                       resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                                       deleting: false,
                                   }, () => {
                                       this.props.onReload(); // Refreshes tableView
                                   });
                                   const opInfo = { // This is what refreshes treeView
                                       operationType: 'DELETE',
                                       resultCode: result.errorCode,
                                       time: Date.now()
                                   };
                                   this.props.setWizardOperationInfo(opInfo);
                               }
                );
            }
        };

        this.handleChangeAck = isAckChecked => {
            this.setState({ isAckChecked });
        };
        // End constructor().
    }

    componentDidMount () {
        getBaseLevelEntryFullAttributes(this.props.editorLdapServer, this.props.wizardEntryDn,
                                        (result) => {
                                            if (result !== '') {
                                                const regex = /^numSubordinates:\s\d+?$/mi;
                                                const line = result.match(regex);
                                                const numSubordinates = line
                                                    ? parseInt(line[0].split(':')[1].trim())
                                                    : 0;
                                                this.setState({
                                                    entryLdifData: result,
                                                    numSubordinates
                                                });
                                            }
                                        }
        );

        const ldapsearchCmd = 'ldapsearch -LLL -o ldif-wrap=no -Y EXTERNAL ' +
            `-b "${this.props.wizardEntryDn}" ` +
            `-H ldapi://%2fvar%2frun%2fslapd-${this.props.editorLdapServer}.socket ` +
            '"(objectClass=*)" \\* +';
        // We should also be searching for objectclass=ldapSubentry, but ldapdelete -r
        // only does a objectclass=* search for its recursive delete
        this.setState({
            ldapsearchCmd
        });
    }

    render () {
        const {
            commandOutput, entryLdifData, numSubordinates,
            resultVariant, isAckChecked, ldapsearchCmd
        } = this.state;

        const info = numSubordinates > 0
            ? _("It has child entries which will also be recursively deleted.")
            : '';
        const entryIcon = numSubordinates > 0
            ? <UsersIcon />
            : <UserIcon />;
        const acknowledgementStep = (
            <div>
                <Card isSelectable>
                    <CardTitle>
                        {_("Delete Acknowledgement")}
                    </CardTitle>
                    <CardBody>
                        {cockpit.format(_("You are about to delete this entry. $0"), info)}
                    </CardBody>
                    <CardBody>
                        {entryIcon}&nbsp;&nbsp;<b className="ds-info-color">{this.props.wizardEntryDn}</b>
                    </CardBody>
                </Card>
            </div>
        );

        let nb = -1;
        const ldifLines = entryLdifData.split('\n').map(line => {
            nb++;
            return { data: line, id: nb };
        });
        const ldifDataStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title={_("LDIF Entry To Be Deleted")}
                />
                <Card isSelectable>
                    { numSubordinates > 0 &&
                        <>
                            <CardBody>
                                {_("Run this command to retrieve the LDAP entries that will be deleted")}
                            </CardBody>
                            <CardBody>
                                <ClipboardCopy
                                    variant={ClipboardCopyVariant.expansion}
                                    isReadOnly
                                    hoverTip={_("Copy")}
                                    clickTip={_("Copied")}
                                >
                                    {ldapsearchCmd}
                                </ClipboardCopy>
                            </CardBody>
                        </>}
                    { numSubordinates === 0 &&
                        <CardBody className="ds-textarea">
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>}
                </Card>
            </div>
        );

        const nbToDelete = numSubordinates > 0
            ? _("This entry, as well as all its child entries,")
            : _("This entry");
        const deletionStep = (
            <div>
                <Alert
                    variant="danger"
                    isInline
                    title={_("This is an irreversible operation!")}
                />
                <div>
                    <Card isSelectable>
                        <CardTitle>
                            {_("Confirm Entry Deletion")}
                        </CardTitle>
                        <CardBody>
                            {cockpit.format(_("$0 will be deleted."), nbToDelete)}
                        </CardBody>
                        <CardBody>
                            {entryIcon}&nbsp;&nbsp;<b className="ds-info-color">{this.props.wizardEntryDn}</b>
                        </CardBody>
                        <CardBody>
                            <Switch
                                isDisabled={this.props.wizardEntryDn === ''}
                                id="ack-switch"
                                label={_("Yes, I'm sure.")}
                                labelOff={_("No, don't delete.")}
                                isChecked={isAckChecked}
                                onChange={this.handleChangeAck}
                            />
                        </CardBody>
                    </Card>
                </div>
            </div>
        );

        let reviewInfo = '';
        if (commandOutput === '') {
            reviewInfo = numSubordinates > 0
                ? _("The entries were")
                : _("The entry was");
            reviewInfo += _(" successfully deleted.");
        } else {
            reviewInfo = _("There was an error during the deletion.");
        }
        const delReviewStep = (
            <div className="ds-margin-bottom-md">
                <Alert
                    variant={resultVariant}
                    title={reviewInfo}
                >
                    {commandOutput}
                </Alert>
                {this.state.deleting &&
                    <div className="ds-center ds-margin-top-xlg">
                        <Spinner size="xlg" />
                    </div>}
            </div>
        );

        const deleteEntrySteps = [
            {
                id: 1,
                name: _("Acknowledgement"),
                component: acknowledgementStep,
                hideBackButton: true,
                canJumpTo: this.state.stepIdReached >= 1 && this.state.stepIdReached < 4
            },
            {
                id: 2,
                name: _("LDIF Data"),
                component: ldifDataStep,
                canJumpTo: this.state.stepIdReached >= 2,
                enableNext: this.state.stepIdReached < 4
            },
            {
                id: 3,
                name: _("Deletion"),
                component: deletionStep,
                nextButtonText: _("Delete"),
                enableNext: isAckChecked,
                canJumpTo: this.state.stepIdReached === 3
            },
            {
                id: 4,
                name: _("Review"),
                component: delReviewStep,
                nextButtonText: _("Finish"),
                canJumpTo: this.state.stepIdReached >= 4,
                hideBackButton: true,
                enableNext: !this.state.deleting,
            }
        ];

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.handleToggleWizard}
                onNext={this.handleNext}
                title={numSubordinates === 0 ? _("Delete LDAP Entry") : _("Delete LDAP Entries")}
                steps={deleteEntrySteps}
            />
        );
    }
}

export default DeleteOperationWizard;
