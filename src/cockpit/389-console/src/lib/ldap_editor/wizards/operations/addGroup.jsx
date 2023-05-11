import cockpit from "cockpit";
import React from 'react';
import {
    Alert,
    Button,
    Card,
    CardBody,
    CardTitle,
    DualListSelector,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    NumberInput,
    Radio,
    Select, SelectOption, SelectVariant,
    SearchInput,
    SimpleList,
    SimpleListItem,
    Spinner,
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
    runGenericSearch,
    decodeLine
} from '../../lib/utils.jsx';

const _ = cockpit.gettext;

class AddGroup extends React.Component {
    constructor (props) {
        super(props);

        // gid range
        this.minValue = 0;
        this.maxValue = 1878982656;

        this.state = {
            parentDN: "",
            groupName: "",
            isTreeLoading: false,
            groupType: "Basic Group",
            isOpenType: false,
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
            stepIdReached: 1,
            noEmptyValue: false,
            adding: true,
            posixGroup: false,
            gidNumber: 0,
            groupDesc: "",
        };

        this.onBaseDnSelection = (treeViewItem) => {
            this.setState({
                usersSearchBaseDn: treeViewItem.dn
            });
        };

        this.showTreeLoadingState = (isTreeLoading) => {
            this.setState({
                isTreeLoading,
                searching: !!isTreeLoading
            });
        };

        this.handleOpenLDAPNavModal = () => {
            this.setState({
                showLDAPNavModal: true
            });
        };

        this.handleCloseLDAPNavModal = () => {
            this.setState({
                showLDAPNavModal: false
            });
        };

        this.handleNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });
            if (id === 5) {
                // Generate the LDIF data.
                this.generateLdifData();
            } else if (id === 6) {
                // Create the LDAP entry.
                const myLdifArray = this.state.ldifArray;
                createLdapEntry(this.props.editorLdapServer,
                                myLdifArray,
                                (result) => {
                                    this.setState({
                                        commandOutput: result.errorCode === 0 ? _("Group successfully created!") : _("Failed to create group, error: ") + result.errorCode,
                                        resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                                        adding: false,
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
                                        relativeDn
                                    };
                                    this.props.setWizardOperationInfo(opInfo);
                                }
                );
            }
        };

        this.handleBack = ({ id }) => {
            if (id === 5) {
                this.updateValuesTableRows(true);
            }
        };

        this.handleSearchClick = () => {
            this.setState({
                isSearchRunning: true,
                usersAvailableOptions: []
            }, () => { this.getEntries() });
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
                baseDn,
                scope: 'sub',
                filter,
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
                        <span title={dnLine} key={dnLine}>
                            {value}
                        </span>
                    );
                });

                this.setState({
                    usersAvailableOptions: newOptionsArray,
                    isSearchRunning: false
                });
            });
        };

        this.removeDuplicates = (options) => {
            const titles = options.map(item => item.props.title);
            const noDuplicates = options
                    .filter((item, index) => {
                        return titles.indexOf(item.props.title) === index;
                    });
            return noDuplicates;
        };

        this.handleUsersOnListChange = (newAvailableOptions, newChosenOptions) => {
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

        // Group Type handling
        this.handleToggleType = isOpenType => {
            this.setState({
                isOpenType
            });
        };
        this.handleSelectType = (event, selection) => {
            this.setState({
                posixGroup: selection === "Posix Group",
                groupType: selection,
                isOpenType: false,
            });
        };

        // gid/uid input handling
        this.onMinusConfig = (id) => {
            if ((Number(this.state[id]) - 1) < 1) {
                return;
            }
            this.setState({
                [id]: Number(this.state[id]) - 1
            });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
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
        });
    }

    generateLdifData = () => {
        const objectClassData = ['objectClass: top'];
        const valueData = [];
        let memberAttr = "";

        if (this.state.posixGroup) {
            objectClassData.push('objectClass: groupOfNames');
            objectClassData.push('objectClass: posixGroup');
            memberAttr = "member";
        } else {
            if (this.state.memberAttr === "uniquemember") {
                objectClassData.push('objectClass: groupOfUniqueNames');
                memberAttr = "uniquemember";
            } else {
                objectClassData.push('objectClass: groupOfNames');
                memberAttr = "member";
            }
        }

        if (this.state.groupDesc !== "") {
            valueData.push(`description: ${this.state.groupDesc}`);
        }
        if (this.state.posixGroup && this.state.gidNumber > 0) {
            valueData.push(`gidNumber: ${this.state.gidNumber}`);
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
    };

    render () {
        const {
            groupName, usersSearchBaseDn, usersAvailableOptions,
            usersChosenOptions, showLDAPNavModal, ldifArray, resultVariant,
            commandOutput, stepIdReached, groupDesc,
        } = this.state;

        const groupSelectStep = (
            <>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Select Group Type")}
                        </Text>
                    </TextContent>

                </div>
                <div className="ds-indent">
                    <Select
                        variant={SelectVariant.single}
                        className="ds-margin-top-lg"
                        aria-label="Select group type"
                        onToggle={this.handleToggleType}
                        onSelect={this.handleSelectType}
                        selections={this.state.groupType}
                        isOpen={this.state.isOpenType}
                    >
                        <SelectOption key="group" value="Basic Group" />
                        <SelectOption key="posix" value="Posix Group" />
                    </Select>
                    <TextContent className="ds-margin-top-xlg">
                        <Text component={TextVariants.h6} className="ds-margin-top-lg ds-font-size-md">
                            <b>{_("Basic Group")}</b>{_("- This type of group can use membership attributes: member, or uniqueMember common set of objectclasses and attributes.")}
                        </Text>
                        <Text component={TextVariants.h6} className="ds-margin-top-lg ds-font-size-md">
                            <b>{_("Posix Group")}</b> {_("- This type of group uses the 'GroupOfNames' and 'PosixGroup' objectclasses which allows for attributes like <i>gidNumber</i>, etc")}
                        </Text>
                    </TextContent>
                </div>
            </>
        );

        const groupNameStep = (
            <div>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Select Name")}
                    </Text>
                </TextContent>
                <Form autoComplete="off">
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={2} className="ds-label">
                            {_("Group Name")}
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
                    <Grid title={_("Optional description for this group.")}>
                        <GridItem span={2} className="ds-label">
                            {_("Group Description")}
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                value={groupDesc}
                                type="text"
                                id="groupDesc"
                                aria-describedby="groupDesc"
                                name="groupDesc"
                                onChange={(str, e) => {
                                    this.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>

                    {!this.state.posixGroup &&
                        <>
                            <TextContent className="ds-margin-top-lg">
                                <Text component={TextVariants.h5}>
                                    {_("Choose The Membership")}
                                </Text>
                            </TextContent>
                            <div className="ds-left-margin">
                                <Radio
                                    name="memberAttrGroup"
                                    id="member"
                                    value="member"
                                    label={_("member")}
                                    isChecked={this.state.memberAttr === 'member'}
                                    onChange={this.handleRadioChange}
                                    description={_("This group uses objectclass 'GroupOfNames'.")}
                                />
                                <Radio
                                    name="memberAttrGroup"
                                    id="uniquemember"
                                    value="uniquemember"
                                    label={_("uniquemember")}
                                    isChecked={this.state.memberAttr === 'uniquemember'}
                                    onChange={this.handleRadioChange}
                                    description={_("This group uses objectclass 'GroupOfUniqueNames'.")}
                                    className="ds-margin-top"
                                />
                            </div>
                        </>}
                    {this.state.posixGroup &&
                        <>
                            <Grid
                                title={_("This setting corresponds to the attribute: gidNumber.  The gid must be greater than zero for this attribute to be set.")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={2}>
                                    {_("Group ID Number")}
                                </GridItem>
                                <GridItem span={10}>
                                    <NumberInput
                                        value={this.state.gidNumber}
                                        min={this.minValue}
                                        max={this.maxValue}
                                        onMinus={() => { this.onMinusConfig("gidNumber") }}
                                        onChange={(e) => { this.onConfigChange(e, "gidNumber", 0) }}
                                        onPlus={() => { this.onPlusConfig("gidNumber") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={8}
                                    />
                                </GridItem>
                            </Grid>
                        </>}
                </Form>
            </div>
        );

        const selectMembersStep = (
            <>
                <Form autoComplete="off">
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    {_("Add Members To The Group")}
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <TextContent>
                                <Text>
                                    {_("Search Base:")}
                                    <Text
                                        className="ds-left-margin"
                                        component={TextVariants.a}
                                        onClick={this.handleOpenLDAPNavModal}
                                        href="#"
                                    >
                                        {usersSearchBaseDn}
                                    </Text>
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top">
                            <SearchInput
                              placeholder={_("Find members...")}
                              value={this.state.searchPattern}
                              onChange={(_event, value) => this.handleSearchPattern(value)}
                              onSearch={this.handleSearchClick}
                              onClear={() => { this.handleSearchPattern('') }}
                            />
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-xlg">
                            <DualListSelector
                                availableOptions={usersAvailableOptions}
                                chosenOptions={usersChosenOptions}
                                availableOptionsTitle={_("Available Members")}
                                chosenOptionsTitle={_("Chosen Members")}
                                onListChange={this.handleUsersOnListChange}
                                id="usersSelector"
                            />
                        </GridItem>

                        <Modal
                            variant={ModalVariant.medium}
                            title={_("Choose A Branch To Search")}
                            isOpen={showLDAPNavModal}
                            onClose={this.handleCloseLDAPNavModal}
                            actions={[
                                <Button
                                    key="confirm"
                                    variant="primary"
                                    onClick={this.handleCloseLDAPNavModal}
                                >
                                    {_("Done")}
                                </Button>
                            ]}
                        >
                            <Card isSelectable className="ds-indent ds-margin-bottom-md">
                                <CardBody>
                                    <LdapNavigator
                                        treeItems={[...this.props.treeViewRootSuffixes]}
                                        editorLdapServer={this.props.editorLdapServer}
                                        skipLeafEntries
                                        handleNodeOnClick={this.onBaseDnSelection}
                                        showTreeLoadingState={this.showTreeLoadingState}
                                    />
                                </CardBody>
                            </Card>
                        </Modal>
                    </Grid>
                </Form>
            </>
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
                    title={_("LDIF Content for Group Creation")}
                />
                <Card isSelectable>
                    <CardBody>
                        { (ldifListItems.length > 0) &&
                            <SimpleList aria-label="LDIF data User">
                                {ldifListItems}
                            </SimpleList>}
                    </CardBody>
                </Card>
            </div>
        );

        let nb = -1;
        const ldifLines = ldifArray.map(line => {
            nb++;
            return { data: line, id: nb };
        });
        const groupReviewStep = (
            <div>
                <Alert
                    variant={resultVariant}
                    isInline
                    title={_("Result for Group Creation")}
                >
                    {commandOutput}
                    {this.state.adding &&
                        <div>
                            <Spinner className="ds-left-margin" size="md" />
                            &nbsp;&nbsp;{_("Adding group ...")}
                        </div>}
                </Alert>
                {resultVariant === 'danger' &&
                    <Card isSelectable>
                        <CardTitle>
                            {_("LDIF Data")}
                        </CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>}
            </div>
        );

        const addGroupSteps = [
            {
                id: 1,
                name: this.props.firstStep[0].name,
                component: this.props.firstStep[0].component,
                canJumpTo: stepIdReached >= 1 && stepIdReached < 6,
                hideBackButton: true
            },
            {
                id: 2,
                name: _("Select Group Type"),
                component: groupSelectStep,
                canJumpTo: stepIdReached >= 2 && stepIdReached < 6,
            },
            {
                id: 3,
                name: _("Select Name"),
                component: groupNameStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 6,
                enableNext: groupName !== '',
            },
            {
                id: 4,
                name: _("Add Members"),
                component: selectMembersStep,
                canJumpTo: stepIdReached >= 4 && stepIdReached < 6,
            },
            {
                id: 5,
                name: _("Create Group"),
                component: groupCreationStep,
                nextButtonText: _("Create"),
                canJumpTo: stepIdReached >= 5 && stepIdReached < 6
            },
            {
                id: 6,
                name: _("Review Result"),
                component: groupReviewStep,
                nextButtonText: _("Finish"),
                canJumpTo: stepIdReached >= 6,
                hideBackButton: true,
                enableNext: !this.state.adding
            }
        ];

        const title = (
            <>
                {_("Parent DN: ")}&nbsp;&nbsp;<strong>{this.props.wizardEntryDn}</strong>
            </>
        );

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.handleToggleWizard}
                onNext={this.handleNext}
                onBack={this.handleBack}
                title={_("Add A Group")}
                description={title}
                steps={addGroupSteps}
            />
        );
    }
}

export default AddGroup;
