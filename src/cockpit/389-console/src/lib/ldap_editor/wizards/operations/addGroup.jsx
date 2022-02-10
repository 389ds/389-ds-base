import React from 'react';
import {
    Alert,
    Button,
    Card,
    CardBody,
    CardTitle,
    Checkbox,
    DualListSelector,
    Form,
    Grid,
    GridItem,
    Label,
    Modal,
    ModalVariant,
    Radio,
    SearchInput,
    SimpleList,
    SimpleListItem,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    ValidatedOptions,
    Wizard,
} from '@patternfly/react-core';
import LdapNavigator from '../../lib/ldapNavigator.jsx';
import {
    createLdapEntry,
    runGenericSearch
} from '../../lib/utils.jsx';
import {
    InfoCircleIcon
} from '@patternfly/react-icons';


class AddGroup extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            parentDN: "",
            groupName: "",
            isTreeLoading: false,
            memberAttr: "member",
            members: [],
            searching: false,
            searchPattern: "",
            showLDAPNavModal: false,
            usersSearchBaseDn: "",
            usersAvailableOptions: [],
            usersChosenOptions: [],
            ldifArray: [],
            resultVariant: 'default',
            commandOutput: "",
            stepIdReached: 0,
            noEmptyValue: false,
        };

        this.handleBaseDnSelection = (treeViewItem) => {
            this.setState({
                usersSearchBaseDn: treeViewItem.dn
            });
        }

        this.showTreeLoadingState = (isTreeLoading) => {
            this.setState({
                isTreeLoading,
                searching: isTreeLoading ? true : false
            });
        }

        this.openLDAPNavModal = () => {
            this.setState({
                showLDAPNavModal: true
            });
        };

        this.closeLDAPNavModal = () => {
            this.setState({
                showLDAPNavModal: false
            });
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            if (id === 3) {
                // Generate the LDIF data.
                this.generateLdifData();
            } else if (id === 4) {
                // Create the LDAP entry.
                const myLdifArray = this.state.ldifArray;
                createLdapEntry(this.props.editorLdapServer,
                    myLdifArray,
                    (result) => {
                        this.setState({
                            commandOutput: result.errorCode === 0 ? 'Group successfully created!' : 'Failed to create group: ' + result.errorCode,
                            resultVariant: result.errorCode === 0 ? 'success' : 'danger'
                        }, () => {
                            this.props.onReload();
                        });
                        // Update the wizard operation information.
                        const myDn = myLdifArray[0].substring(4);
                        const relativeDn = myLdifArray[3].replace(": ", "="); // cn val
                        const opInfo = {
                            operationType: 'ADD',
                            resultCode: result.errorCode,
                            time: Date.now(),
                            entryDn: myDn,
                            relativeDn: relativeDn
                        }
                        this.props.setWizardOperationInfo(opInfo);
                    }
                );
            }
        };

        this.onBack = ({ id }) => {
            if (id === 3) {
                // true ==> Do not check the attribute selection when navigating back.
                this.updateValuesTableRows(true);
            }
        };

        this.handleSearchClick = () => {
            this.setState({
                isSearchRunning: true,
                usersAvailableOptions: []
            }, () => { this.getEntries () });
        };

        this.handleSearchPattern = searchPattern => {
            this.setState({ searchPattern });
        };

        this.getEntries = () => {
            const baseDn = this.state.usersSearchBaseDn;
            const pattern = this.state.searchPattern;
            const filter = pattern === ''
                ? '(|(objectClass=person)(objectClass=nsPerson))'
                : `(&(|(objectClass=person)(objectClass=nsPerson)(objectClass=nsAccount)(objectClass=nsOrgPerson)(objectClass=posixAccount))(|(cn=*${pattern}*)(uid=${pattern})))`;
            const attrs = 'cn uid';

            const params = {
                serverId: this.props.editorLdapServer,
                baseDn: baseDn,
                scope: 'sub',
                filter: filter,
                attributes: attrs
            };
            runGenericSearch(params, (resultArray) => {
                const newOptionsArray = resultArray.map(result => {
                    const lines = result.split('\n');
                    // TODO: Currently picking the first value found.
                    // Might be worth to take the value that is used as RDN in case of multiple values.

                    // Handle base64-encoded data:
                    const pos0 = lines[0].indexOf(':: ');
                    const pos1 = lines[1].indexOf(':: ');

                    let dnLine = lines[0];
                    if (pos0 > 0) {
                        const decoded = decodeLine(dnLine);
                        dnLine = `${decoded[0]}: ${decoded[1]}`;
                    }
                    const value = pos1 === -1
                    ? (lines[1]).split(': ')[1]
                    : decodeLine(lines[1])[1];

                    return (
                        <span title={dnLine}>
                            {value}
                        </span>
                    );
                });

                this.setState({
                    usersAvailableOptions: newOptionsArray,
                    isSearchRunning: false
                });
            });
        }

        this.removeDuplicates = (options) => {
            const titles = options.map(item => item.props.title);
            const noDuplicates = options
                .filter((item, index) => {
                    return titles.indexOf(item.props.title) === index;
                });
            return noDuplicates;
        };

        this.usersOnListChange = (newAvailableOptions, newChosenOptions) => {
            const newAvailNoDups = this.removeDuplicates(newAvailableOptions);
            const newChosenNoDups = this.removeDuplicates(newChosenOptions);

            this.setState({
                usersAvailableOptions: newAvailNoDups.sort(),
                usersChosenOptions: newChosenNoDups.sort()
            });

        };

        this.handleRadioChange = (_, event) => {
            this.setState({
                memberAttr: event.currentTarget.id,
            });
        };

        this.handleChange = this.handleChange.bind(this);
    }

    componentDidMount () {
        this.setState({
            parentDN: this.props.wizardEntryDn,
            usersSearchBaseDn: this.props.wizardEntryDn,
        });
    }

    handleChange (e) {
        const attr = e.target.id;
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [attr]: value,
        })
    }

    generateLdifData = () => {
        const objectClassData = ['objectClass: top'];
        const valueData = [];
        let memberAttr = "";

        if (this.state.memberAttr === "uniquemember") {
            objectClassData.push('objectClass: groupOfUniqueNames');
            memberAttr = "uniquemember";
        } else {
            objectClassData.push('objectClass: groupOfNames');
            memberAttr = "member";
        }

        for (const userObj of this.state.usersChosenOptions) {
            const dn_val = userObj.props.title.replace(/^dn: /, "");
            valueData.push(`${memberAttr}: ${dn_val}`);
        }

        const ldifArray = [
            `dn: cn=${this.state.groupName},${this.props.wizardEntryDn}`,
            ...objectClassData,
            `cn: ${this.state.groupName}`,
            ...valueData
        ];
        this.setState({ ldifArray });
    }

    render () {
        const {
            groupName, usersSearchBaseDn, usersAvailableOptions,
            usersChosenOptions, showLDAPNavModal, ldifArray, resultVariant,
            commandOutput, stepIdReached
        } = this.state;

        const groupNameAndTypeStep = (
            <div>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Select Name And Group Type
                    </Text>
                </TextContent>
                <Form autoComplete="off">
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={2} className="ds-label">
                            Group Name
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                value={groupName}
                                type="text"
                                id="groupName"
                                aria-describedby="groupName"
                                name="groupName"
                                onChange={(str, e) => {
                                    this.handleChange(e);
                                }}
                                validated={this.state.groupName === '' ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>

                    <TextContent className="ds-margin-top-lg">
                        <Text component={TextVariants.h5}>
                            Choose The Membership Attribute
                        </Text>
                    </TextContent>
                    <div className="ds-left-margin">
                        <Radio
                            name="memberAttrGroup"
                            id="member"
                            value="member"
                            label="member"
                            isChecked={this.state.memberAttr === 'member'}
                            onChange={this.handleRadioChange}
                            description="This attribute uses objectclass 'GroupOfNames'"
                        />
                        <Radio
                            name="memberAttrGroup"
                            id="uniquemember"
                            value="uniquemember"
                            label="uniquemember"
                            isChecked={this.state.memberAttr === 'uniquemember'}
                            onChange={this.handleRadioChange}
                            description="This attribute uses objectclass 'GroupOfUniqueNames'"
                            className="ds-margin-top"
                        />
                    </div>
                </Form>
            </div>
        );

        const selectMembersStep = (
            <React.Fragment>
                <Form autoComplete="off">
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Add Members To The Group
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <Label onClick={this.openLDAPNavModal} href="#" variant="outline" color="blue" icon={<InfoCircleIcon />}>
                                Search Base DN
                            </Label>
                            <strong>&nbsp;&nbsp;{usersSearchBaseDn}</strong>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top">
                            <SearchInput
                              placeholder="Find members..."
                              value={this.state.searchPattern}
                              onChange={this.handleSearchPattern}
                              onSearch={this.handleSearchClick}
                              onClear={() => { this.handleSearchPattern('') }}
                            />
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <DualListSelector
                                availableOptions={usersAvailableOptions}
                                chosenOptions={usersChosenOptions}
                                availableOptionsTitle="Available Members"
                                chosenOptionsTitle="Chosen Members"
                                onListChange={this.usersOnListChange}
                                id="usersSelector"
                            />
                        </GridItem>

                        <Modal
                            variant={ModalVariant.medium}
                            title="Choose A Branch To Search"
                            isOpen={showLDAPNavModal}
                            onClose={this.closeLDAPNavModal}
                            actions={[
                                <Button
                                    key="confirm"
                                    variant="primary"
                                    onClick={this.closeLDAPNavModal}
                                >
                                    Done
                                </Button>
                            ]}
                        >
                            <Card isHoverable className="ds-indent ds-margin-bottom-md">
                                <CardBody>
                                    <LdapNavigator
                                        treeItems={[...this.props.treeViewRootSuffixes]}
                                        editorLdapServer={this.props.editorLdapServer}
                                        skipLeafEntries={true}
                                        handleNodeOnClick={this.handleBaseDnSelection}
                                        showTreeLoadingState={this.showTreeLoadingState}
                                    />
                                </CardBody>
                            </Card>
                        </Modal>
                    </Grid>
                </Form>
            </React.Fragment>
        );

        const ldifListItems = ldifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const groupCreationStep = (
            <div>
                <Alert
                    variant="info"
                    isInline
                    title="LDIF Content for Group Creation"
                />
                <Card isHoverable>
                    <CardBody>
                        { (ldifListItems.length > 0) &&
                            <SimpleList aria-label="LDIF data User">
                                {ldifListItems}
                            </SimpleList>
                        }
                    </CardBody>
                </Card>
            </div>
        );

        let nb = -1;
        const ldifLines = ldifArray.map(line => {
            nb++;
            return { data: line, id: nb };
        })
        const groupReviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title="Result for Group Creation"
                >
                    {commandOutput}
                </Alert>
                {resultVariant === 'danger' &&
                    <Card isHoverable>
                        <CardTitle>
                            LDIF Data
                        </CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>
                }
            </div>
        );

        const addGroupSteps = [
            {
                id: 1,
                name: 'Select Name & Type',
                component: groupNameAndTypeStep,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 4,
                enableNext: groupName === '' ? false : true,
                hideBackButton: true
            },
            {
                id: 2,
                name: 'Add Members',
                component: selectMembersStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 4,
            },
            {
                id: 3,
                name: 'Create Group',
                component: groupCreationStep,
                nextButtonText: 'Create',
                canJumpTo: stepIdReached >= 3 && stepIdReached < 4
            },
            {
                id: 4,
                name: 'Review Result',
                component: groupReviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached >= 4,
                hideBackButton: true
            }
        ];

        const title = <>
            Parent DN: &nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
        </>;

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                onNext={this.onNext}
                onBack={this.onBack}
                title="Add A Group"
                description={title}
                steps={addGroupSteps}
            />
        );
    }
}

export default AddGroup;
