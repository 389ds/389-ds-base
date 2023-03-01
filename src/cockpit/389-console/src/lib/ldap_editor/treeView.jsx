import React from 'react';
import {
    Alert, AlertGroup, AlertActionCloseButton, AlertVariant,
    Button,
    Bullseye,
    Card, CardBody, CardFooter, CardTitle,
    CardHeaderMain, CardHeader, CardActions,
    Divider,
    DropdownItem, Dropdown, DropdownSeparator,
    EmptyState, EmptyStateIcon, EmptyStateBody, EmptyStateVariant,
    Grid, GridItem,
    KebabToggle,
    Label,
    Spinner,
    Title,
    TextContent, Text, TextVariants, TextList,
    TextListVariants, TextListItem, TextListItemVariants,
    Tooltip
} from '@patternfly/react-core';
import {
    ArrowRightIcon,
    CatalogIcon,
    CubeIcon,
    DomainIcon,
    ExclamationCircleIcon,
    ExclamationTriangleIcon,
    InfoCircleIcon,
    LockIcon,
    ResourcesEmptyIcon,
    SyncAltIcon,
    UserIcon,
    UsersIcon,
} from '@patternfly/react-icons';
import { breakWord } from '@patternfly/react-table';
import GenericPagination from './lib/genericPagination.jsx';
import LdapNavigator from './lib/ldapNavigator.jsx';
import CreateRootSuffix from './lib/rootSuffix.jsx';
import { ENTRY_MENU } from './lib/constants.jsx';
import { log_cmd } from "../tools.jsx";
import {
    showCertificate,
    b64DecodeUnicode
} from './lib/utils.jsx';

class EditorTreeView extends React.Component {
    constructor (props) {
        super(props);

        this.attributesSpecialHandling = [
            'jpegphoto',
            'usercertificate', 'usercertificate;binary',
            'cacertificate', 'cacertificate;binary',
            'userpassword'
        ];

        this.tableEmtpyStateRow = [{
            showAnEmptyTable: true, // Used in the render() for a quick check.
            heightAuto: true,
            cells: [{
                props: { colSpan: 8 },
                title: (
                    <Bullseye>
                        <EmptyState variant={EmptyStateVariant.small}>
                            <EmptyStateIcon icon={ResourcesEmptyIcon} />
                            <Title headingLevel="h2" size="lg">
                                No entry is selected
                            </Title>
                            <EmptyStateBody>
                                Select an entry to see its details.
                            </EmptyStateBody>
                        </EmptyState>
                    </Bullseye>)
            }]
        }];

        this.state = {
            alerts: [],
            firstClickOnTree: false,
            searchInput: '',
            rowsValues: [],
            entryColumns: [
                { title: 'Attribute' },
                // { title: 'Value', transforms: [wrappable] }],
                { title: 'Value', cellTransforms: [breakWord] }],
            entryRows: [],
            entryIsLoading: true,
            entryIcon: null,
            entryDn: '',
            entryState: '',
            entryStateIcon: null,
            isSuffixEntry: false,
            entryModTime: '',
            isEmptySuffix: false,
            showPagination: false,
            isEntryTooLarge: false,
            // To trigger an update in GenericPagination.componentDidUpdate().
            // Maybe just use a random number?
            tableModificationTime: 0,
            showEmptySuffixModal: false,
            newSuffixData: { creationDate: 0 },
            isTreeLoading: false,
            refreshButtonTriggerTime: 0,
            latestEntryRefreshTime: 0,
            searching: false,
            isRole: false
        };

        this.addAlert = (title, variant, key) => {
            this.setState({
                alerts: [...this.state.alerts, { title: title, variant: variant, key }]
            });
        };

        this.removeAlert = key => {
            this.setState({ alerts: [...this.state.alerts.filter(el => el.key !== key)] });
        };

        this.handleNodeOnClick = (treeViewItem) => {
            if (treeViewItem && treeViewItem.dn === this.state.entryDn) {
                // Clicking on already selected node, just return
                this.updateEntryRows(treeViewItem);
                return;
            }

            this.setState({
                searching: true,
            }, () => {
                if (!this.state.firstClickOnTree) {
                    this.setState({
                        firstClickOnTree: true
                    });
                    // Update the parent state so that the menu option 'Hide Entry Details' is enabled.
                    this.props.setTreeFirstClicked(true);
                }
                // Update the table showing the entry details.
                this.updateEntryRows(treeViewItem);
            });
        };

        this.handleEmptySuffixToggle = (resultParams) => {
            this.setState(({ showEmptySuffixModal }) => ({
                showEmptySuffixModal: !showEmptySuffixModal
            }));
            if (resultParams.message) {
                const creationDate = Date.now();
                this.addAlert(resultParams.message, resultParams.variant, creationDate);
                if (resultParams.variant === 'success') {
                    const rootEntryData = resultParams.rootEntryData;
                    this.setState({
                        newSuffixData: { rootEntryData, creationDate },
                    }, () => { this.props.onReload(true) });
                }
            }
        };
    } // End constructor.

    componentDidMount () {
        this.props.setTreeFirstClicked(false);
    }

    componentDidUpdate (prevProps) {
        // Make sure to hide the entry details after a reload ( when instance selection changes ).
        if (this.props.loading !== prevProps.loading) {
            this.setState({ firstClickOnTree: false });
            this.props.setTreeFirstClicked(false);
        }
    }

    showEntryLoading = (isEntryLoading) => {
        this.setState({
            searching: isEntryLoading ? true : false
        });
    };

    showTreeLoadingState = (isTreeLoading) => {
        this.setState({
            isTreeLoading,
        });
    }

    updateEntryRows = (treeViewItem) => {
        // Handle the case where a selected entry has been deleted ==> Show an empty table.
        if (treeViewItem === null) {
            this.setState({
                entryRows: this.tableEmtpyStateRow,
                tableModificationTime: Date.now(), // Passed as property.
                entryModTime: '', // An empty value will not be rendered.
                entryIcon: null,
                entryDn: '', // An empty value disables the actions dropdown menu.
                latestEntryRefreshTime: Date.now(),
                searching: false,
            });
            return;
        }

        // Always close the drop down
        this.props.onToggleEntryMenu(false);

        let entryRows = [];
        const isEmptySuffix = treeViewItem.isEmptySuffix;
        let entryIcon = treeViewItem.icon; // Only already set for special suffixes.
        let entryStateIcon = "";
        const entryDn = treeViewItem.dn === '' ? 'Root DSE' : treeViewItem.dn;
        const isSuffixEntry = treeViewItem.id === "0";
        const entryModTime = treeViewItem.modTime;
        const fullEntry = treeViewItem.fullEntry;
        let encodedValues = [];
        let isRole = false;
        fullEntry
            .filter(data => (data.attribute + data.value !== '') && // Filter out empty lines
            (data.attribute !== '???: ')) // and data for empty suffix(es) and in case of failure.
            .map(line => {
                if (line.attribute !== undefined) {
                    const attr = line.attribute;
                    const attrLowerCase = attr.trim().toLowerCase();
                    let val = line.value.substring(1);

                    if (line.value.substring(0, 2) === '::') {
                        if (this.attributesSpecialHandling.includes(attrLowerCase)) {
                            // Let the encoded value be added to the table first.
                            // Keep the index where the value will be inserted ( current length of the array).
                            // Once the decoding is done, insert the decoded value at the relevant index.
                            encodedValues.push({ index: entryRows.length, line: line });
                        } else {
                            // TODO: Check why the decoding of 'nssymmetrickey is failing...
                            if (attrLowerCase === 'nssymmetrickey') {
                                val = line.value.substring(3);
                            } else {
                                val = b64DecodeUnicode(line.value.substring(3));
                            }
                        }
                    }

                    if (attr.toLowerCase() === "userpassword") {
                        val = "********";
                    }

                    entryRows.push([{ title: <strong>{attr}</strong> }, val]);
                    const myVal = val.trim().toLowerCase();
                    const accountObjectclasses = ['nsaccount', 'nsperson', 'simplesecurityobject',
                                                  'organization', 'person', 'account', 'organizationalunit',
                                                  'netscapeserver', 'domain', 'posixaccount', 'shadowaccount',
                                                  'posixgroup', 'mailrecipient', 'nsroledefinition'];
                    if (accountObjectclasses.includes(myVal)) {
                        entryStateIcon = <LockIcon className="ds-pf-blue-color"/>
                    }
                    if (myVal === 'nsroledefinition') {
                        isRole = true;
                    }
                    // TODO: Use a better logic to assign icons!
                    // console.log(`!entryIcon = ${!entryIcon}`);
                    if (!entryIcon && attrLowerCase === 'objectclass') {
                        // console.log(`val.trim().toLowerCase() = ${val.trim().toLowerCase()}`);
                        if (myVal === 'inetorgperson' || myVal === 'posixaccount' || myVal === 'person') {
                            entryIcon = <UserIcon/>
                        } else if (myVal === 'organizationalunit' || myVal === 'groupofuniquenames' || myVal === 'groupofnames') {
                            entryIcon = <UsersIcon/>
                        } else if (myVal === 'domain') {
                            entryIcon = <DomainIcon/>
                        }
                    }
                } else {
                    // Value too large Label
                    entryRows.push([{ title: <strong>{line.props.attr}</strong> }, line]);
                }
            });

        // Set the default icon if needed.
        if (!entryIcon) {
            entryIcon = isEmptySuffix ? <ResourcesEmptyIcon/> : <CubeIcon/>;
        }

        // Show a warning message if the entry is truncated.
        // console.log(`fullEntry = ${fullEntry}`);
        const lastLine = fullEntry.length > 0
            ? fullEntry.slice(-1).pop()
            : {};
        const isEntryTooLarge = isEmptySuffix
            ? false
            : lastLine.attribute === 'MESSAGE' && lastLine.value === ':ENTRY TOO LARGE - OUTPUT TRUNCATED!';

        // Update the rows of the selected entry.
        const entryIsLoading = false;
        let entryState = "";

        const cmd = ["dsidm", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.editorLdapServer + ".socket",
            "-b", entryDn, isRole ? "role" : "account", "entry-status", entryDn];
        log_cmd("updateEntryRows", "Checking if entry is activated", cmd);
        cockpit
            .spawn(cmd, { superuser: true, err: 'message' })
            .done(content => {
                if ((entryDn !== 'Root DSE') && (entryStateIcon !== "")) {
                    const status = JSON.parse(content);
                    entryState = status.info.state;
                    if (entryState === 'inactivity limit exceeded' || entryState.startsWith("probably activated or")) {
                        entryStateIcon = <ExclamationTriangleIcon className="ds-pf-yellow-color ct-icon-exclamation-triangle"/>
                    }
                }
            })
            .fail(err => {
                const errMsg = JSON.parse(err);
                if ((entryDn !== 'Root DSE') && (entryStateIcon !== "") && !(errMsg.desc.includes("Root suffix can't be locked or unlocked"))) {
                    console.error(
                        "updateEntryRow",
                        `${isRole ? "role" : "account"} account entry-status operation failed`,
                        errMsg.desc
                    );
                    entryState = "error: please, check browser logs";
                    entryStateIcon = <ExclamationCircleIcon className="ds-pf-red-color ct-exclamation-circle"/>
                }
            })
            .finally(() => {
                const tableModificationTime = Date.now();
                this.setState({
                    entryRows,
                    entryDn,
                    entryState,
                    isSuffixEntry,
                    entryModTime,
                    isEmptySuffix,
                    entryIsLoading,
                    isEntryTooLarge,
                    tableModificationTime,
                    entryIcon,
                    entryStateIcon,
                    isRole
                },
                () => {
                    // Now decode the encoded values.
                    // A sample object stored in the variable encodedValues looks like { index: entryRows.length, line: line }
                    const finalRows = [...this.state.entryRows];
                    let numberDecoded = 0;
                    // console.log(`encodedValues.length = ${encodedValues.length}`);

                    encodedValues.map(myObj => {
                        const attr = myObj.line.attribute;
                        // console.log('Processing attribute = ' + attr);
                        const attrLowerCase = attr.trim().toLowerCase();
                        const encVal = myObj.line.value.substring(3); // eg ==> "jpegPhoto:: <VALUE>". Removing 2 colons and 1 space character.
                        let decodedValue = encVal; // Show the encoded value in case the decoding fails.

                        // See list of attribute types:
                        // https://pagure.io/389-ds-console/blob/master/f/src/com/netscape/admin/dirserv/propedit/DSPropertyModel.java
                        switch (attrLowerCase) {
                            case 'jpegphoto':
                            {
                                decodedValue =
                                    <React.Fragment>
                                        <img
                                            src={`data:image/png;base64,${encVal}`}
                                            alt=''
                                            style={{ width: '256px' }} // height will adjust automatically.
                                        />
                                    </React.Fragment>;

                                // Use the picture as an icon:
                                const myPhoto = <img
                                    src={`data:image/png;base64,${encVal}`}
                                    alt=''
                                    style={{ width: '48px' }} // height will adjust automatically.
                                />
                                const newRow = [{ title: <strong>{attr}</strong> }, decodedValue];
                                finalRows.splice(myObj.index, 1, newRow);
                                numberDecoded++;

                                this.setState({ entryIcon: myPhoto });
                                break;
                            }
                            case 'userpassword':
                                numberDecoded++;
                                break;
                            case 'usercertificate':
                            case 'usercertificate;binary':
                            case 'cacertificate':
                            case 'cacertificate;binary':
                                showCertificate(encVal,
                                    (result) => {
                                        // const dataArray = [];
                                        if (result.code === 'OK') {
                                            const timeDiff = result.timeDifference;
                                            const certDays = Math.ceil(Math.abs(timeDiff) / (1000 * 3600 * 24));
                                            const dayMsg = certDays > 1
                                                ? `${certDays} days`
                                                : `${certDays} day`;
                                            const diffMessage = timeDiff > 0
                                                ? `Certificate is valid for ${dayMsg}`
                                                : `Certificate is expired since ${dayMsg}`;
                                            const type = timeDiff < 0
                                                ? 'danger'
                                                : timeDiff < (1000 * 3600 * 24 * 30) // 30 days.
                                                    ? 'warning'
                                                    : 'info';
                                            const certItems = result.data
                                            .map(datum => {
                                                return (
                                                    <React.Fragment key={datum.param} >
                                                        <TextListItem component={TextListItemVariants.dt}>{datum.param}</TextListItem>
                                                        <TextListItem component={TextListItemVariants.dd}>{datum.paramVal}</TextListItem>
                                                    </React.Fragment>);
                                            });

                                            decodedValue =
                                                (<React.Fragment>
                                                    <div>
                                                        <Alert variant={type} isInline title={diffMessage} />
                                                        <TextContent>
                                                            <TextList component={TextListVariants.dl}>
                                                                {certItems}
                                                            </TextList>
                                                        </TextContent>
                                                    </div>
                                                </React.Fragment>);

                                            const newRow = [{ title: <strong>{attr}</strong> }, decodedValue];
                                            finalRows.splice(myObj.index, 1, newRow);
                                            numberDecoded++;

                                            if (encodedValues.length === numberDecoded) {
                                                // Caution: We need to update the entryRows here
                                                // ( AFTER the decoding of the certificate is completed ).
                                                // The decoding is done asynchronously in showCertificate()!
                                                this.setState({
                                                    entryRows: finalRows,
                                                    tableModificationTime: Date.now()
                                                });
                                            }
                                        }
                                    });

                                break;
                            default:
                                console.log(`Got an unexpected line ==> ${myObj.line}`);
                                console.log(`Got an unexpected line attr ==> ${myObj.line.attribute}`);
                                console.log(`Got an unexpected line value ==> ${myObj.line.value}`);
                        }
                        // Update the entry table once all encoded attributes have been processed:
                        if (encodedValues.length === numberDecoded) {
                            this.setState({
                                entryRows: finalRows,
                                tableModificationTime: Date.now(),
                            });
                        }
                    });

                    // Update the refresh time.
                    this.setState({
                        latestEntryRefreshTime: Date.now(),
                    }, () => { this.showEntryLoading(false) });
                });
            });
    };

    render () {
        const {
            alerts, searching, isSuffixEntry, isRole,
            firstClickOnTree, entryColumns, entryRows, entryIcon, entryDn, entryModTime, isEmptySuffix,
            entryIsLoading, isEntryTooLarge, tableModificationTime, showEmptySuffixModal, entryState,
            newSuffixData, isTreeLoading, refreshButtonTriggerTime, latestEntryRefreshTime, entryStateIcon
        } = this.state;

        const { loading } = this.props;
        let lockingDropdown = [];
        if (entryState !== "" && !entryState.startsWith("error:")) {
            if (entryState !== "activated") {
                if (entryState.includes("probably activated") || entryState.includes("indirectly locked")) {
                    lockingDropdown = [<DropdownItem
                                            key="tree-view-lock"
                                            component="button"
                                            name={isRole ? ENTRY_MENU.lockRole : ENTRY_MENU.lockAccount}
                                            value={entryDn}
                                        >
                                            Lock ...
                                        </DropdownItem>];
                } else {
                    lockingDropdown = [<DropdownItem
                                            key="tree-view-unlock"
                                            component="button"
                                            name={isRole ? ENTRY_MENU.unlockRole : ENTRY_MENU.unlockAccount}
                                            value={entryDn}
                                        >
                                            Unlock ...
                                        </DropdownItem>];
                }
            } else {
                lockingDropdown = [<DropdownItem
                                        key="tree-view-lock"
                                        component="button"
                                        name={isRole ? ENTRY_MENU.lockRole : ENTRY_MENU.lockAccount}
                                        value={entryDn}
                                    >
                                        Lock ...
                                    </DropdownItem>];
            }
        }

        const dropdownItems = [
            <DropdownItem
                key="tree-view-search"
                component="button"
                name={ENTRY_MENU.search}
                value={entryDn}
            >
                Search ...
            </DropdownItem>,
            <DropdownSeparator key="separator-2" />,
            <DropdownItem
                key="tree-view-edit"
                component="button"
                name={ENTRY_MENU.edit}
                value={entryDn}
                // TODO: It won't be a good user experience to prevent the edition of large entries...
                // Either increase the limit ( to 2 MB ? ) or retrieve the data and bypass the limit.
                isDisabled={isEntryTooLarge}
            >
                Edit ...
            </DropdownItem>,
            <DropdownItem
                key="tree-view-new"
                component="button"
                name={ENTRY_MENU.new}
                value={entryDn}
            >
                New ...
            </DropdownItem>,
            <DropdownItem
                key="tree-view-rename"
                component="button"
                name={ENTRY_MENU.rename}
                value={entryDn}
                isDisabled={isSuffixEntry}
            >
                Rename ...
            </DropdownItem>,
            // Lock and Unlock buttons
            ...lockingDropdown,
            <DropdownItem
                key="tree-view-acis"
                component="button"
                name={ENTRY_MENU.acis}
                value={entryDn}
            >
                ACIs ...
            </DropdownItem>,
            <DropdownItem
                key="tree-view-cos"
                component="button"
                name={ENTRY_MENU.cos}
                value={entryDn}
            >
                Class of Service ...
            </DropdownItem>,
            /*
            <DropdownItem
                isDisabled
                key="tree-view-refs"
                component="button"
                name="smartRefs"
                value={entryDn}
            >
                Smart Referrals ...
            </DropdownItem>,
            */
            <DropdownSeparator key="separator-3" />,
            <DropdownItem
                key="tree-view-delete"
                component="button"
                name={ENTRY_MENU.delete}
                value={entryDn}
            >
                Delete ...
            </DropdownItem>,
        ];

        const loadingStateComponent = (
            <>
                <div className="ds-margin-top-lg ds-indent">
                    <Label icon={<CatalogIcon />} color="blue">
                        <strong>{this.props.treeViewRootSuffixes.length > 1 ? "Suffix Trees" : "Suffix Tree"}</strong>
                        <Button
                            variant="link"
                            title="Refresh all the suffixes"
                            icon={<SyncAltIcon />}
                            onClick={() => { this.props.onReload(true) }}
                        />
                    </Label>
                </div>
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="lg" />
                </div>
            </>
        );

        const loadingEntryComponent = (
            <div className="ds-margin-top-xlg ds-center">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Loading ...
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="lg" />
            </div>
        );


        const finishedAt = new Date();
        finishedAt.setTime(this.props.timeOfCompletion);

        const specialSuffCount = (this.props.treeViewRootSuffixes
            .filter(suff => suff.isSpecial)).length;

        const isValidData = entryDn
            ? true
            // The entry DN was null or undefined.
            : entryRows[0]
                ? entryRows[0].showAnEmptyTable // Set to true when an entry is deleted.
                : false; // Got an invalid data for some reason...

        const entryDnLowerCase = isValidData
            ? entryDn.toLowerCase()
            : null;

        let page_body = "";
        if (loading) {
            page_body = loadingStateComponent;
        } else {
            page_body =
                <div>
                    {(alerts.length > 0) &&
                        <AlertGroup isToast>
                            {alerts.map(({ key, variant, title }) => (
                                <Alert
                                    isLiveRegion
                                    variant={AlertVariant[variant]}
                                    title={title}
                                    timeout={true}
                                    actionClose={
                                        <AlertActionCloseButton
                                            title="Suffix creation"
                                            variantLabel={`${variant} alert`}
                                            onClose={() => this.removeAlert(key)}
                                        />
                                    }
                                    key={key}
                                />
                            ))}
                        </AlertGroup>
                    }

                    <Grid hasGutter className="ds-margin-top-lg ds-indent">
                        <GridItem span={6}>
                            <Label icon={<CatalogIcon />} color="blue">
                                <strong>{this.props.treeViewRootSuffixes.length > 1 ? "Suffix Trees" : "Suffix Tree"}</strong>
                                <Button
                                    variant="link"
                                    title="Refresh all the suffixes"
                                    icon={<SyncAltIcon />}
                                    onClick={() => { this.props.onReload(true) }}
                                />
                            </Label>
                            { !loading &&
                                <LdapNavigator
                                    key={this.props.loading}
                                    treeItems={[...this.props.treeViewRootSuffixes]}
                                    timeOfCompletion={this.props.timeOfCompletion}
                                    editorLdapServer={this.props.editorLdapServer}
                                    wizardOperationInfo={this.props.wizardOperationInfo}
                                    newSuffixData={newSuffixData}
                                    handleNodeOnClick={this.handleNodeOnClick}
                                    updateEntryRows={this.updateEntryRows}
                                    refreshEntryTime={this.props.refreshEntryTime}
                                    showTreeLoadingState={this.showTreeLoadingState}
                                    refreshButtonTriggerTime={refreshButtonTriggerTime}
                                    handleEntryRefresh={this.refreshEntry}
                                    addNotification={this.props.addNotification}
                                    isDisabled={isTreeLoading || searching}
                                />
                            }
                        </GridItem>
                        <GridItem span={6}>
                            { firstClickOnTree &&
                                <Label icon={<InfoCircleIcon />} color="blue" >
                                    <strong>Entry Details</strong>
                                    <Tooltip
                                        position="top"
                                        content={
                                            <div>Reload the LDAP entry.</div>
                                        }
                                    >
                                        <Button
                                            variant="link"
                                            icon={<SyncAltIcon />}
                                            onClick={() => this.setState({ refreshButtonTriggerTime: Date.now() })}
                                        />
                                    </Tooltip>
                                </Label>
                            }
                            <div className= "ds-margin-bottom" />

                            { searching && loadingEntryComponent }

                            { firstClickOnTree && !loading && !searching && isValidData &&
                                <Card isSelectable>
                                    <CardHeader>
                                        <CardActions>
                                            <Dropdown
                                                onSelect={this.props.onSelectEntryOptions}
                                                toggle={<KebabToggle
                                                    isDisabled={isEmptySuffix || (entryDn === '')}
                                                    onToggle={this.props.onToggleEntryMenu}
                                                />}
                                                isOpen={this.props.entryMenuIsOpen}
                                                isPlain
                                                dropdownItems={dropdownItems}
                                                position={'right'}
                                            />
                                        </CardActions>
                                        <CardHeaderMain>
                                            {entryIcon}
                                        </CardHeaderMain>
                                        <Title headingLevel="h6" size="md">
                                            <span>&ensp;{entryDn} </span>
                                            {(entryState !== "") && (entryStateIcon !== "") && (entryState !== "activated")?
                                            <Tooltip
                                                position="bottom"
                                                content={
                                                    <div>
                                                        {entryState}
                                                    </div>
                                                }
                                            >
                                                <a className="ds-font-size-md">{entryStateIcon}</a>
                                            </Tooltip>
                                            : ""}
                                        </Title>
                                    </CardHeader>
                                    { isEntryTooLarge &&
                                        <CardTitle>
                                            <Label color="orange" icon={<InfoCircleIcon />} >
                                                Entry is too large - Table is truncated.
                                            </Label>
                                        </CardTitle>
                                    }

                                    <CardBody>
                                        <GenericPagination
                                            columns={entryColumns}
                                            rows={entryRows}
                                            tableModificationTime={tableModificationTime}
                                            ariaLabel="Entry details table with pagination"
                                        />

                                        { isEmptySuffix &&
                                            <EmptyState variant={EmptyStateVariant.small}>
                                                <EmptyStateIcon icon={ResourcesEmptyIcon} />
                                                <Title headingLevel="h2" size="lg">
                                                    Empty suffix!
                                                </Title>
                                                <EmptyStateBody>
                                                    <Label variant="outline" color="orange" icon={<InfoCircleIcon />}>
                                                        The suffix is configured, but it has no entries.
                                                    </Label>
                                                    <Button
                                                        className="ds-margin-top-lg"
                                                        variant="link"
                                                        onClick={this.handleEmptySuffixToggle}
                                                    >
                                                        Create the root suffix entry <ArrowRightIcon />
                                                    </Button>
                                                </EmptyStateBody>
                                            </EmptyState>
                                        }
                                        { showEmptySuffixModal &&
                                            <CreateRootSuffix
                                                showEmptySuffixModal={showEmptySuffixModal}
                                                handleEmptySuffixToggle={this.handleEmptySuffixToggle}
                                                suffixDn={entryDn}
                                                editorLdapServer={this.props.editorLdapServer}
                                                updateEntryRows={this.updateEntryRows}
                                            />
                                        }
                                    </CardBody>
                                    { !isEmptySuffix && (entryModTime.length > 0) &&
                                        <CardFooter>
                                            Last Modified Time: {entryModTime}
                                            <div className="ds-margin-bottom-md" />
                                            <Divider />
                                            <div className="ds-margin-bottom-md" />
                                            <Label variant="outline" color="blue" >
                                                Last Refresh at {(new Date(latestEntryRefreshTime)).toLocaleString()}
                                            </Label>
                                            <Tooltip
                                                position="top"
                                                content={
                                                    <div>Reload the LDAP entry.</div>
                                                }
                                            >
                                                <Button
                                                    variant="link"
                                                    icon={<SyncAltIcon />}
                                                    onClick={() => this.setState({ refreshButtonTriggerTime: Date.now() })}
                                                />
                                            </Tooltip>
                                        </CardFooter>
                                    }
                                </Card>
                            }
                        </GridItem>
                    </Grid>
                </div>;
        }

        return (
            <React.Fragment>
                {page_body}
            </React.Fragment>
        );
    }
}

export default EditorTreeView;
