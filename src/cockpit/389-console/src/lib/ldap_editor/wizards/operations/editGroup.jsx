import React from 'react';
import {
    Button,
    Card,
    CardBody,
    Divider,
    DualListSelector,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    SearchInput,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextInput,
    TextVariants,
} from '@patternfly/react-core';
import {
    UsersIcon
} from '@patternfly/react-icons';
import LdapNavigator from '../../lib/ldapNavigator.jsx';
import {
    runGenericSearch,
    decodeLine,
    getBaseDNFromTree,
    modifyLdapEntry,
    getBaseLevelEntryAttributes,
} from '../../lib/utils.jsx';
import { DoubleConfirmModal } from "../../../notifications.jsx";
import GroupTable from './groupTable.jsx';

class EditGroup extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            activeTabKey: 0,
            saving: false,
            isTreeLoading: false,
            members: [],
            searching: false,
            searchPattern: "",
            showLDAPNavModal: false,
            usersSearchBaseDn: "",
            usersAvailableOptions: [],
            usersChosenOptions: [],
            ldifArray: [],
            showConfirmMemberDelete: false,
            showConfirmBulkDelete: false,
            modalOpen: true,
            modalSpinning: false,
            modalChecked: false,
            delMember: "",
            bulkDeleting: false,
            delMemberList: [],
            showViewEntry: false,
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            event.preventDefault();
            this.setState({
                activeTabKey: tabIndex
            });
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

        this.closeModal = () => {
            this.setState({
                modalOpen: false
            });
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
            const filter = pattern === '' || pattern === '*'
                ? '(|(objectClass=person)(objectClass=nsPerson)(objectClass=nsAccount)(objectClass=nsOrgPerson)(objectClass=posixAccount))'
                : `(&(|(objectClass=person)(objectClass=nsPerson)(objectClass=nsAccount)(objectClass=nsOrgPerson)(objectClass=posixAccount))(|(cn=*${pattern}*)(uid=*${pattern}*)))`;
            const attrs = 'dn';

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
                    const dnEncoded = lines[0].indexOf(':: ');
                    let dnLine = lines[0];
                    if (dnEncoded > 0) {
                        const decoded = decodeLine(dnLine);
                        dnLine = decoded[1];
                    } else {
                        dnLine = dnLine.substring(4);
                    }

                    // Is this dn already chosen?
                    for (const cOption of this.state.usersChosenOptions) {
                        if (cOption.props.title === dnLine) {
                            return "skip";
                        }
                    }

                    // Check here is member is already in the group
                    if (this.props.members.includes(dnLine)) {
                        return (
                            <span className="ds-pf-green-color" title={dnLine + " (ALREADY A MEMBER)"}>
                                {dnLine}
                            </span>
                        );
                    } else {
                        return (
                            <span title={dnLine}>
                                {dnLine}
                            </span>
                        );
                    }
                }).filter(member_option => member_option !== "skip");

                newOptionsArray.sort((a, b) => (a.props.children > b.props.children ? 1 : -1));
                this.setState({
                    usersAvailableOptions: newOptionsArray,
                    isSearchRunning: false
                });
            });
        }

        this.usersOnListChange = (newAvailableOptions, newChosenOptions) => {
            let newNewAvailOptions = [...newAvailableOptions];
            let newNewChosenOptions = []

            for (const option of newChosenOptions) {
                if ('className' in option.props && option.props.className.indexOf("ds-pf-green-color") !== -1) {
                    // This member is in the group already(green), put it back
                    newNewAvailOptions.push(option);
                } else {
                    // This member is not in the group, allow it
                    newNewChosenOptions.push(option);
                }
            }

            this.setState({
                usersAvailableOptions: newNewAvailOptions.sort((a, b) => (a.props.children > b.props.children ? 1 : -1)),
                usersChosenOptions: newNewChosenOptions.sort((a, b) => (a.props.children > b.props.children ? 1 : -1))
            });
        };

        this.showConfirmMemberDelete = this.showConfirmMemberDelete.bind(this);
        this.closeConfirmMemberDelete = this.closeConfirmMemberDelete.bind(this);
        this.showConfirmBulkDelete = this.showConfirmBulkDelete.bind(this);
        this.closeConfirmBulkDelete = this.closeConfirmBulkDelete.bind(this);
        this.addMembers = this.addMembers.bind(this);
        this.delMember = this.delMember.bind(this);
        this.onSelectMember = this.onSelectMember.bind(this);
        this.delBulkMembers = this.delBulkMembers.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.switchEditor = this.switchEditor.bind(this);
        this.editEntry = this.editEntry.bind(this);
        this.viewEntry = this.viewEntry.bind(this);
        this.closeViewEntry = this.closeViewEntry.bind(this);
    };

    componentDidMount() {
        this.setState({
            members: [...this.props.members],
            _members: [...this.props.members],
            usersSearchBaseDn: getBaseDNFromTree(this.props.groupdn, this.props.treeViewRootSuffixes)
        });
    }

    switchEditor () {
        this.setState({
            modalOpen: false,
        }, () => {
            this.props.onReload(1);
            this.props.useGenericEditor();
        });
    }

    editEntry (memberdn) {
        this.setState({
            modalOpen: false,
        });
        this.props.openEditEntry(memberdn);
    }

    viewEntry (memberdn) {
        getBaseLevelEntryAttributes(this.props.editorLdapServer,
            memberdn,
            (entryDetails) => {
                let ldifArray = [];
                entryDetails
                .filter(data => (data.attribute + data.value !== '' && // Filter out empty lines
                data.attribute !== '???: ')) // and data for empty suffix(es) and in case of failure.
                .map((line, index) => {
                    if (index === 1000) {
                        ldifArray.push({
                            'attribute': '... Truncated',
                            'value': " - Entry too large to display ..."
                        });
                        return;
                    } else if (index > 1000) {
                        return;
                    }
                    ldifArray.push(line);
                });
                this.setState({
                    showViewEntry: true,
                    ldifArray: ldifArray,
                });
        });
    }

    closeViewEntry () {
        this.setState({
            showViewEntry: false,
            ldifArray: [],
        });
    }

    handleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    showConfirmMemberDelete (name) {
        let ldifArray = [];
        let memberAttr = "uniquemember";

        if (this.props.isGroupOfNames) {
            memberAttr = "member";
        }
        ldifArray.push(`dn: ${this.props.groupdn}`);
        ldifArray.push('changetype: modify');
        ldifArray.push(`delete:  ${memberAttr}`);
        ldifArray.push(`${memberAttr}: ${name}`);

        this.setState({
            showConfirmMemberDelete: true,
            delMember: name,
            modalChecked: false,
            modalSpinning: false,
            ldifArray: ldifArray,
        });
    }

    closeConfirmMemberDelete() {
        this.setState({
            showConfirmMemberDelete: false,
            modalSpinning: false,
            modalChecked: false,
            delMember: "",
        });
    }

    addMembers () {
        const params = { serverId: this.props.editorLdapServer };
        let ldifArray = [];
        let memberAttr = "uniquemember";
        let memCount = 0;

        this.setState({
            saving: true,
        });

        if (this.props.isGroupOfNames) {
            memberAttr = "member";
        }
        ldifArray.push(`dn: ${this.props.groupdn}`);
        ldifArray.push('changetype: modify');

        // Loop of dual select list
        for (const user of this.state.usersChosenOptions) {
            ldifArray.push(`add:  ${memberAttr}`);
            ldifArray.push(`${memberAttr}: ${user.props.title}`);
            ldifArray.push('-');
            memCount += 1;
        }

        modifyLdapEntry(params, ldifArray, (result) => {
            if (result.errorCode === 0) {
                this.props.addNotification(
                    "success",
                    "Successfully added " + memCount + " member" + (memCount > 1 ? "s" : "")
                );
            } else {
                this.props.addNotification(
                    "error",
                    "Failed to update group, error code: " + result.errorCode
                );
            }
            this.setState({
                saving: false,
                usersChosenOptions: [],
            }, () => { this.props.onReload(1) });
        });
    }

    delMember () {
        const params = { serverId: this.props.editorLdapServer };
        modifyLdapEntry(params, this.state.ldifArray, (result) => {
            if (result.errorCode === 0) {
                this.props.addNotification(
                    "success",
                    "Successfully updated group"
                );
            } else {
                this.props.addNotification(
                    "error",
                    "Failed to update group, error code: " + result.errorCode
                );
            }
            this.setState({
                showConfirmMemberDelete: false,
                showConfirmBulkDelete: false,
                bulkDeleting: false,
            }, () => { this.props.onReload(1) });
        });
    }

    onSelectMember(memberdn, isSelected) {
        let delMemList = [...this.state.delMemberList];
        if (isSelected) {
            if (!delMemList.includes(memberdn)) {
                // Add member to delete
                delMemList.push(memberdn);
            }
        } else {
            let idx = delMemList.indexOf(memberdn);
            if (idx !== -1) {
                // Remove member from delete list
                delMemList.splice(idx, 1);
            }
        }

        this.setState({
            delMemberList: delMemList
        });
    };

    showConfirmBulkDelete () {
        this.setState({
            showConfirmBulkDelete: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmBulkDelete () {
        this.setState({
            showConfirmBulkDelete: false,
        });
    }

    delBulkMembers () {
        if (this.state.delMemberList.length === 0) {
            return;
        }
        this.setState({
            bulkDeleting: true,
        });
        let ldifArray = [];
        let memberAttr = "uniquemember";

        if (this.props.isGroupOfNames) {
            memberAttr = "member";
        }
        ldifArray.push(`dn: ${this.props.groupdn}`);
        ldifArray.push('changetype: modify');
        for (const member of this.state.delMemberList) {
            ldifArray.push(`delete:  ${memberAttr}`);
            ldifArray.push(`${memberAttr}: ${member}`);
            ldifArray.push(`-`);
        }
        this.setState({
            ldifArray: ldifArray,
        }, () => this.delMember() );
    }

    render () {
        const {
            usersSearchBaseDn, usersAvailableOptions,
            usersChosenOptions, showLDAPNavModal, members, modalOpen,
            saving, delMemberList, showViewEntry, ldifArray,
        } = this.state;
        const title = <><UsersIcon />&nbsp;&nbsp;{this.props.groupdn}</>;
        const extraPrimaryProps = {};
        let saveBtnName = "Add Members";
        if (saving) {
            saveBtnName = "Saving ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        const delMemList = this.state.delMemberList.map((member) =>
            <div key={member} className="ds-left-margin ds-left-align">{member}</div>);

        return (
            <div>
                <Modal
                    className="ds-modal-select-tall"
                    variant={ModalVariant.large}
                    title={title}
                    isOpen={modalOpen}
                    onClose={this.closeModal}
                    actions={[
                        <Button
                            key="switch"
                            variant="secondary"
                            onClick={this.switchEditor}
                            className="ds-float-right"
                        >
                            Switch To Generic Editor
                        </Button>,
                    ]}
                >
                    <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab
                            eventKey={0}
                            title={<TabTitleText>Current Members <font size="2">({this.props.members.length})</font></TabTitleText>}
                        >
                            <GroupTable
                                key={members + delMemberList}
                                rows={members}
                                removeMember={this.showConfirmMemberDelete}
                                onSelectMember={this.onSelectMember}
                                showConfirmBulkDelete={this.showConfirmBulkDelete}
                                delMemberList={delMemberList}
                                viewEntry={this.viewEntry}
                                editEntry={this.props.openEditEntry}
                                saving={this.state.bulkDeleting}
                            />
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>Find New Members</TabTitleText>}>
                            <Form autoComplete="off">
                                <Grid className="ds-indent">
                                    <GridItem span={12} className="ds-margin-top-xlg">
                                        <TextContent>
                                            <Text>
                                                Search Base:
                                                <Text
                                                    className="ds-left-margin"
                                                    component={TextVariants.a}
                                                    onClick={this.openLDAPNavModal}
                                                    href="#"
                                                >
                                                    {usersSearchBaseDn}
                                                </Text>
                                            </Text>
                                        </TextContent>
                                    </GridItem>
                                    <GridItem span={12} className="ds-margin-top-lg">
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
                                        <Button
                                            className="ds-margin-top"
                                            isDisabled={usersChosenOptions.length === 0 || this.state.saving}
                                            variant="primary"
                                            onClick={this.addMembers}
                                            isLoading={this.state.saving}
                                            spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                            {...extraPrimaryProps}
                                        >
                                            {saveBtnName}
                                        </Button>
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
                                        <Card isSelectable className="ds-indent ds-margin-bottom-md">
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
                                    <Divider
                                        className="ds-margin-top-lg"
                                    />
                                </Grid>
                            </Form>
                        </Tab>
                    </Tabs>
                    <Modal
                        variant={ModalVariant.medium}
                        title="View Entry"
                        isOpen={showViewEntry}
                        onClose={this.closeViewEntry}
                    >
                        <Card isSelectable className="ds-indent ds-margin-bottom-md">
                            <CardBody className="ds-textarea">
                                {ldifArray.map((line) => (
                                    <h6 key={line.attribute+line.value}>{line.attribute}{line.value}</h6>
                                ))}
                            </CardBody>
                        </Card>
                    </Modal>
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmMemberDelete}
                        closeHandler={this.closeConfirmMemberDelete}
                        handleChange={this.handleModalChange}
                        actionHandler={this.delMember}
                        spinning={this.state.modalSpinning}
                        item={this.state.delMember}
                        checked={this.state.modalChecked}
                        mTitle="Remove Member From Group"
                        mMsg="Are you sure you want to remove this member?"
                        mSpinningMsg="Deleting ..."
                        mBtnName="Delete"
                    />
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmBulkDelete}
                        closeHandler={this.closeConfirmBulkDelete}
                        handleChange={this.handleModalChange}
                        actionHandler={this.delBulkMembers}
                        spinning={this.state.modalSpinning}
                        item={delMemList}
                        checked={this.state.modalChecked}
                        mTitle="Remove Members From Group"
                        mMsg="Are you sure you want to remove these members?"
                        mSpinningMsg="Deleting ..."
                        mBtnName="Delete Members"
                    />
                </Modal>
            </div>
        );
    }
}

export default EditGroup;
