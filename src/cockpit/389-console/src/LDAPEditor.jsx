import cockpit from "cockpit";
import React from 'react';
import {
    expandable
} from '@patternfly/react-table';
import {
    AngleRightIcon,
    CatalogIcon,
    ExclamationCircleIcon,
    ExclamationTriangleIcon,
    InfoCircleIcon,
    LockIcon,
    ResourcesEmptyIcon,
} from '@patternfly/react-icons';
import {
    Breadcrumb,
    BreadcrumbItem,
    Button,
    Modal,
    ModalVariant,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextList,
    TextListItem,
    TextVariants,
    Tooltip,
} from "@patternfly/react-core";
import {
    generateUniqueId,
    getUserSuffixes,
    getRootSuffixEntryDetails,
    getOneLevelEntries,
    getBaseLevelEntryAttributes,
    getAllObjectClasses,
} from './lib/ldap_editor/lib/utils.jsx';
import CreateRootSuffix from './lib/ldap_editor/lib/rootSuffix.jsx';
import { ENTRY_MENU } from './lib/ldap_editor/lib/constants.jsx';
import EditorTableView from './lib/ldap_editor/tableView.jsx';
import EditorTreeView from './lib/ldap_editor/treeView.jsx';
import { SearchDatabase } from './lib/ldap_editor/search.jsx';
import GenericWizard from './lib/ldap_editor/wizards/genericWizard.jsx';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import { log_cmd } from "./lib/tools.jsx";

export class LDAPEditor extends React.Component {
    constructor (props) {
        super(props);

        this.rootSuffixesRows = [];
        this.rootSuffixesTreeData = [];
        this.initialChildText = 'Loading...';

        this.state = {
            activeTabKey: 0,
            firstLoad: true,
            keyIndex: 0,
            suffixList: [],
            changeLayout: false,
            openNewEntry: false,
            openDeleteEntry: false,
            baseDN: "",
            emptyDN: "",
            loading: true,
            searching: false,
            perPage: 50,
            page: 0,
            total: 0,
            onRootSuffixes: true,
            numberSuffixes: 0,
            entryMenuIsOpen: false,
            wizardOperationInfo: { operationType: '', resultCode: -1, time: 0 },
            wizardName: '',
            isWizardOpen: false,
            isTreeWizardOpen: false,
            showEmptySuffixModal: false,
            wizardEntryDn: '',
            treeViewRootSuffixes: [],
            searchBase: "",
            isEntryTooLarge: false,
            refreshEntryTime: 0,
            navItems: [
                { id: 'home', label: <CatalogIcon />, active: false },
                { id: 'db-suffixes', to: '#', label: 'Database Suffixes', active: true }
            ],
            columns: [
                {
                    title: 'Database Suffixes',
                    cellFormatters: [expandable]
                },
                {
                    title: 'Child Entries'
                },
                {
                    title: 'Last Modified'
                }
            ],
            rows: [],
            pagedRows: [],
            refreshing: false,
            allObjectclasses: [],
            isConfirmModalOpen: false,
            isTreeViewAction: false,
            currentRowKey: -1
        };

        this.handleConfirmModalToggle = () => {
            this.setState(({ isConfirmModalOpen }) => ({
                isConfirmModalOpen: !isConfirmModalOpen,
            }));
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        // Actions when user clicks on the Entry Details menu
        this.handleToggleEntryMenu = isOpen => {
            this.setState({
                entryMenuIsOpen: isOpen
            });
        };

        this.handleSelectEntryOptions = event => {
            // Tree view update
            const aTarget = event.target;

            // No need to use a wizard for the Refresh operation.
            if (aTarget.name === ENTRY_MENU.refresh) {
                this.setState({
                    entryMenuIsOpen: !this.state.entryMenuIsOpen,
                    refreshEntryTime: Date.now()
                });
                return;
            }
            if (aTarget.name === ENTRY_MENU.search) {
                this.setState({
                    activeTabKey: 2,
                    searchBase: aTarget.value
                });
                return;
            }
            if (aTarget.name === ENTRY_MENU.lockRole) {
                this.setState({
                    entryDn: aTarget.value,
                    entryType: "role",
                    operationType: "lock",
                    isTreeViewAction: true
                }, () => { this.handleConfirmModalToggle() });
                return;
            }
            if (aTarget.name === ENTRY_MENU.lockAccount) {
                this.setState({
                    entryDn: aTarget.value,
                    entryType: "account",
                    operationType: "lock",
                    isTreeViewAction: true
                }, () => { this.handleConfirmModalToggle() });
                return;
            }
            if (aTarget.name === ENTRY_MENU.unlockRole) {
                this.setState({
                    entryDn: aTarget.value,
                    entryType: "role",
                    operationType: "unlock",
                    isTreeViewAction: true
                }, () => { this.handleConfirmModalToggle() });
                return;
            }
            if (aTarget.name === ENTRY_MENU.unlockAccount) {
                this.setState({
                    entryDn: aTarget.value,
                    entryType: "account",
                    operationType: "unlock",
                    isTreeViewAction: true
                }, () => { this.handleConfirmModalToggle() });
                return;
            }

            const keyIndex = this.state.keyIndex + 1;
            this.setState({
                entryMenuIsOpen: false,
                wizardName: aTarget.name,
                isTreeWizardOpen: true,
                isWizardOpen: false,
                wizardEntryDn: aTarget.value,
                keyIndex,
            });
        };

        this.toggleOpenTreeWizard = () => {
            this.setState({
                isTreeWizardOpen: !this.state.isTreeWizardOpen
            });
        };

        this.handleReloadNoop = () => {
            // Treeview does not require a reload
        };

        this.toggleOpenWizard = () => {
            this.setState({
                isWizardOpen: !this.state.isWizardOpen
            });
        };

        // Set the operation type and the result code
        this.setWizardOperationInfo = (opInfo) => {
            // typeAndResult is an object with two fields (opType and result)
            // eg: {operationType: 'MODRDN', resultCode: 0, time: 1613090160492}
            const wizardOperationInfo = { ...opInfo };
            this.setState({
                wizardOperationInfo,
            });
        };

        // Show entry details table when user first clicks on the tree:
        this.setTreeFirstClicked = (value) => {
            this.setState({ treeVisibleAndFirstClicked: value });
        };

        this.onHandleEmptySuffixToggle = (dn) => {
            const showEmptySuffixModal = this.state.showEmptySuffixModal;
            this.setState({
                showEmptySuffixModal: !showEmptySuffixModal,
                emptyDN: dn,
            }, () => { this.handleReload(true) });
        };

        this.handleCollapse = this.handleCollapse.bind(this);
        this.onNavItemClick = this.onNavItemClick.bind(this);
        this.handleReload = this.handleReload.bind(this);
        this.getAttributes = this.getAttributes.bind(this);
        this.handleLockUnlockEntry = this.handleLockUnlockEntry.bind(this);
    }

    handleReload(refresh) {
        const params = {
            serverId: this.props.serverId,
            baseDn: this.state.baseDN,
            addNotification: this.props.addNotification,
        };

        if (this.state.firstLoad) {
            this.setState({
                firstLoad: false
            });
        }

        this.setState({
            searching: true,
            loading: refresh
        });

        if (this.state.baseDN === "Database Suffixes" || this.state.baseDN === "db-suffixes" || this.state.baseDN === "") {
            this.rootSuffixesTreeData = [];
            this.showSuffixes('Database Suffixes', null);
        } else {
            let parentId = 0;
            this.state.suffixList.map(suffixDN => {
                const params = {
                    serverId: this.props.serverId,
                    baseDn: suffixDN,
                    parentId: parentId,
                    addNotification: this.props.addNotification,
                };

                getRootSuffixEntryDetails(params, this.updateTableRootSuffixes);
                parentId += 2; // The next DN row will be two rows below.
            });
            getOneLevelEntries(params, this.processDirectChildren);
        }
    }

    handleLockUnlockEntry() {
        const {
            entryDn,
            entryType,
            operationType,
            isTreeViewAction
        } = this.state;

        const cmd = ["dsidm", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "-b", entryDn, entryType, operationType, entryDn];
        log_cmd("handleLockUnlockEntry", `${operationType} entry`, cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: 'message' })
                .done(_ => {
                    this.setState({
                        entryMenuIsOpen: !this.state.entryMenuIsOpen,
                    }, () => {
                        this.handleConfirmModalToggle();
                        if (isTreeViewAction) {
                            this.setState({
                                refreshEntryTime: Date.now(),
                                isTreeViewAction: false
                            });
                        } else {
                            this.handleReload(true);
                        }
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    console.error(
                        "handleLockUnlockEntry",
                        `${entryType} ${operationType} operation failed -`,
                        errMsg.desc
                    );
                    this.props.addNotification(
                        `${errMsg.desc.includes(`is already ${operationType === "unlock" ? "active" : "locked"}`) ? 'warning' : 'error'}`,
                        `${errMsg.desc}`
                    );
                    this.setState({
                        entryMenuIsOpen: !this.state.entryMenuIsOpen,
                    }, () => {
                        this.handleConfirmModalToggle();
                        if (isTreeViewAction) {
                            this.setState({
                                refreshEntryTime: Date.now(),
                                isTreeViewAction: false
                            });
                        } else {
                            this.handleReload(true);
                        }
                    });
                });
    }

    getAttributes(callbackFunc) {
        const attr_cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "list"
        ];
        log_cmd("getAttributes", "Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const attrContent = JSON.parse(content);
                    const attrs = [];
                    for (const content of attrContent.items) {
                        attrs.push(content.name[0]);
                    }

                    this.setState({
                        attributes: attrs,
                    }, () => { callbackFunc('Database Suffixes', null) });
                });
    }

    componentDidMount () {
        getAllObjectClasses(this.props.serverId, (ocs) => {
            this.setState({
                allObjectclasses: ocs,
            }, () => { this.getAttributes(this.showSuffixes) });
        });
    }

    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(7)) {
            if (this.state.firstLoad) {
                this.handleReload(true);
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.handleReload(true);
                }
            }
        }
    }

    getPageData (page, perPage) {
        if (page === 1) {
            const pagedRows = this.state.rows.slice(0, 2 * perPage); // Each parent has a single child.
            this.setState({ pagedRows, perPage, page, loading: false });
        } else {
            // Need the double since each parent has a single child.
            const start = 2 * (page - 1) * perPage;
            const end = 2 * page * perPage;
            const pagedRows = this.state.rows.slice(start, end);
            for (let i = 0; i < pagedRows.length - 1; i++) {
                if (i % 2 === 0) {
                    pagedRows[i + 1].parent = i;
                }
            }
            this.setState({ pagedRows, perPage, page, loading: false });
        }
    }

    onNavItemClick = (id, active) => {
        if ((id === 'home') || (active)) { // Nothing to do.
            return;
        }

        if (id === 'db-suffixes') {
            const navItems = this.state.navItems.slice(0, 2);
            this.setState({
                navItems,
                loading: true,
                baseDN: id,
            });
            this.showSuffixes('Database Suffixes', null);
        } else {
            // Find the item position.
            const listOfItems = this.state.navItems;
            let position;
            // Skip the first two elements ( Home and Database Suffixes )
            // and the last one ( should be inactive ).
            for (position = 2; position < listOfItems.length - 1; position++) {
                if (listOfItems[position].id === id) {
                    break;
                }
            }

            const navItems = this.state.navItems.slice(0, position + 1);
            const len = navItems.length;
            navItems[len - 1].active = true;
            this.setState({
                navItems,
                baseDN: id,
                searching: true,
            });
            const params = {
                serverId: this.props.serverId,
                baseDn: id,
                addNotification: this.props.addNotification,
            };
            getOneLevelEntries(params, this.processDirectChildren);
        }
    }

    // Handle clicks on the numSubordinates links.
    handleClickNumSubordinates = (dn) => {
        const listOfItems = this.state.navItems;
        const itemsLength = listOfItems.length;
        let totalParentLength = 0;
        let pos = 0;
        while (pos < itemsLength) {
            listOfItems[pos].active = false;
            if (pos > 1) { // The first two elements are not part of the DN.
                totalParentLength += (listOfItems[pos].label.length + 1); // Count the separating comma as well.
            }
            pos++;
        }

        // Remove the parent DN in order to keep only the RDN.
        // TODO: This should be better handled by an LDAP specific function
        // to retrieve the RDN from a full DN!
        let newLabel = dn;
        if (itemsLength > 2) { // We're after the "Database Suffixes" link.
            const dnLength = dn.length;
            newLabel = dn.slice(0, dnLength - totalParentLength);
        }

        const newNavItems = [
            ...listOfItems,
            { id: dn, to: '#', label: newLabel, active: true }
        ];

        // TODO:  Also called from showSuffixes().
        // Use a new fucntion that will called from these two places
        // or use a state info.
        const tableTitle = this.state.columns[0].title;
        // TODO ==> To optimze!
        // if ((isRootSuff && (tableTitle !== 'Database Suffixes')) ||
        // (!isRootSuff && (tableTitle === 'Database Suffixes'))) {
        // const firstTitle = (label === 'Database Suffixes') ? 'Database Suffixes' : 'Entry DN'
        if (tableTitle === 'Database Suffixes') {
            this.setState(prevState => ({
                columns: [
                    {
                        title: 'Entry DN',
                        cellFormatters: [expandable]
                    },
                    ...prevState.columns.slice(1)
                ]
            }));
        }
        this.setState({
            navItems: newNavItems,
            baseDN: dn,
            searching: true,
        });
        const params = {
            serverId: this.props.serverId,
            baseDn: dn,
            addNotification: this.props.addNotification,
        };
        getOneLevelEntries(params, this.processDirectChildren);
    }

    // Process the entries that are direct children.
    processDirectChildren = (directChildren, params) => {
        this.setState({
            loading: true
        });
        const childrenRows = [];
        let rowNumber = 0;

        directChildren.map(aChild => {
            const info = JSON.parse(aChild);
            const numSubCellInfo = parseInt(info.numSubordinates) > 0
                ? {
                    title:
                    <a href="#" onClick={(e) => this.handleClickNumSubordinates(info.dn, false, e)}>
                        {info.numSubordinates} <AngleRightIcon />
                    </a>
                }
                : info.numSubordinates;

            // TODO Test for a JPEG photo!!!
            // if ( info.fullEntry.contains)

            // TODO Add isActive func
            let dn = info.dn;
            if (info.ldapsubentry) {
                dn =
                    <div className="ds-info-icon">
                        {info.dn} <InfoCircleIcon title="This is a hidden LDAP subentry" className="ds-info-icon" />
                    </div>;
            }

            childrenRows.push(
                {
                    isOpen: false,
                    cells: [
                        { title: dn },
                        numSubCellInfo,
                        info.modifyTimestamp,
                    ],
                    rawdn: info.dn,
                    entryState: "",
                    isRole: info.isRole,
                    isLockable: info.isLockable,
                    ldapsubentry: info.ldapsubentry
                },
                {
                    // customRowId: info.parentId + 1,
                    parent: rowNumber, // info.parentId,
                    cells: [
                        { title: this.initialChildText }
                    ]
                });

            // Increment by 2 the row number.
            rowNumber += 2;
        });

        this.setState({
            loading: false,
            searching: false,
            rows: childrenRows,
            // Each row is composed of a parent and its single child.
            pagedRows: childrenRows.slice(0, 2 * this.state.perPage),
            total: childrenRows.length / 2,
            page: 1
        }, () => {
            if (this.state.currentRowKey >= 0) {
                this.handleCollapse(null, this.state.currentRowKey, true, null);
            }
        });
    }

    handleCollapse (event, rowKey, isOpen, data) {
        const { pagedRows } = this.state;
        // TODO
        /**
        * Please do not use rowKey as row index for more complex tables.
        * Rather use some kind of identifier like ID passed with each row.
        */
        pagedRows[rowKey].isOpen = isOpen;
        // Update the row now so that it doesn't stay closed while the
        // LDAP search operations is going on.
        this.setState({
            pagedRows
        });

        const firstTime = (pagedRows[rowKey + 1].cells[0].title) === this.initialChildText;
        if (firstTime) {
            const entryRows = [];
            let entryStateIcon = "";
            let isRole = false;
            const entryDn = pagedRows[rowKey].rawdn; // The DN is the first element in the array.
            getBaseLevelEntryAttributes(this.props.serverId, entryDn, (entryArray) => {
                entryArray.map(line => {
                    const attr = line.attribute;
                    const attrJpegVal = line.attribute.toLowerCase() === "jpegphoto"
                        ?
                            <div>
                                <img
                                    src={`data:image/png;base64,${line.value.substr(3)}`} // strip ':: '
                                    alt=''
                                    style={{ width: '256px' }} // height will adjust automatically.
                                />
                            </div>
                        : line.value;
                    const val = line.value.toLowerCase() === ": ldapsubentry" ? <span className="ds-info-color">{line.value}</span> : line.attribute.toLowerCase() === "userpassword" ? ": ********" : attrJpegVal;

                    // <div key={line.attribute + line.value}></div>
                    entryRows.push({ attr: attr, value: val });

                    const myVal = line.value.substring(1).trim()
                            .toLowerCase();
                    const accountObjectclasses = ['nsaccount', 'nsperson', 'simplesecurityobject',
                        'organization', 'person', 'account', 'organizationalunit',
                        'netscapeserver', 'domain', 'posixaccount', 'shadowaccount',
                        'posixgroup', 'mailrecipient', 'nsroledefinition'];
                    if (accountObjectclasses.includes(myVal)) {
                        entryStateIcon = <LockIcon className="ds-pf-blue-color" />;
                    }
                    if (myVal === 'nsroledefinition') {
                        isRole = true;
                    }
                });
                let entryState = "";
                const cmd = ["dsidm", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                    "-b", entryDn, isRole ? "role" : "account", "entry-status", entryDn];
                log_cmd("handleCollapse", "Checking if entry is activated", cmd);
                cockpit
                        .spawn(cmd, { superuser: true, err: 'message' })
                        .done(content => {
                            if ((entryDn !== 'Root DSE') && (entryStateIcon !== "")) {
                                const status = JSON.parse(content);
                                entryState = status.info.state;
                                if (entryState === 'inactivity limit exceeded' || entryState.startsWith("probably activated or")) {
                                    entryStateIcon = <ExclamationTriangleIcon className="ds-pf-yellow-color ct-icon-exclamation-triangle" />;
                                }
                            }
                        })
                        .fail(err => {
                            const errMsg = JSON.parse(err);
                            if ((entryDn !== 'Root DSE') && (entryStateIcon !== "") && !(errMsg.desc.includes("Root suffix can't be locked or unlocked"))) {
                                console.error(
                                    "handleCollapse",
                                    `${isRole ? "role" : "account"} account entry-status operation failed`,
                                    errMsg.desc
                                );
                                entryState = "error: please, check browser logs";
                                entryStateIcon = <ExclamationCircleIcon className="ds-pf-red-color ct-exclamation-circle" />;
                            }
                        })
                        .finally(() => {
                            let entryStateIconFinal = "";
                            if ((entryState !== "") && (entryStateIcon !== "") && (entryState !== "activated")) {
                                entryStateIconFinal =
                                    <Tooltip
                                        position="bottom"
                                        content={
                                            <div className="ds-info-icon">
                                                {entryState}
                                            </div>
                                        }
                                    >
                                        <a className="ds-font-size-md">{entryStateIcon}</a>
                                    </Tooltip>;
                            }
                            pagedRows[rowKey].entryState = entryState;
                            pagedRows[rowKey + 1].cells = [{
                                title: (
                                    <>
                                        {entryRows.map((line) => {
                                            let attrLine = "";
                                            if (line.attr === "dn") {
                                                attrLine =
                                                    <div key={line.attr + line.value}>
                                                        <strong>{line.attr}</strong>{line.value} {entryStateIconFinal}
                                                    </div>;
                                            } else {
                                                attrLine =
                                                    <div key={line.attr + line.value}>
                                                        <strong>{line.attr}</strong>{line.value}
                                                    </div>;
                                            }
                                            return attrLine;
                                        }
                                        )}
                                    </>
                                )
                            }];
                            // Update the rows.
                            this.setState({
                                pagedRows,
                                currentRowKey: -1
                            });
                        });
            });
        }
    }

    // Update the table rows after getting details of the root suffixes.
    updateTableRootSuffixes = (entryDetails) => {
        const info = JSON.parse(entryDetails);
        if (info.dn === null) {
            console.log('updateTableRootSuffixes: Got a NULL DN! Object "info" ===> ' + info);
            return;
        }

        const isEmptySuffix = (info.errorCode === 32);
        const suffixCellInfo = !isEmptySuffix
            ? info.dn
            : {
                title: (
                    <div className="ds-warning-icon">
                        <ExclamationTriangleIcon /> {info.dn}
                    </div>
                )
            };

        let key = 0;
        const numSubCellInfo = parseInt(info.numSubordinates) > 0
            ? {
                title:
                <a href="#" key={'link-' + info.dn} onClick={(e) => this.handleClickNumSubordinates(info.dn, true, e)}>
                    {info.numSubordinates} <AngleRightIcon />
                </a>
            }
            : info.numSubordinates;

        let node_present = false;
        for (const node of this.rootSuffixesRows) {
            if (node.rawdn === info.dn) {
                node_present = true;
                break;
            }
        }
        if (!node_present) {
            this.rootSuffixesRows = [...this.rootSuffixesRows,
                {
                    isOpen: false,
                    cells: [
                        suffixCellInfo,
                        numSubCellInfo,
                        info.modifyTimestamp
                    ],
                    rawdn: info.dn,
                    customRowId: info.parentId,
                    isEmptySuffix: isEmptySuffix,
                    entryState: ""
                },
                {
                    customRowId: info.parentId + 1,
                    parent: info.parentId,
                    cells: [{
                        title: !isEmptySuffix
                            ? (
                                <>
                                    {info.fullEntry.map((line) => (
                                        <div key={key++}>
                                            <strong> {line.attribute}</strong>
                                            {line.value}
                                        </div>
                                    ))}
                                </>
                            )
                            : (
                                <div>
                                    <strong>&nbsp;<em>This suffix is empty!&nbsp;&nbsp;&nbsp;</em></strong>
                                    <a key={'top-entry-' + info.dn} href="#" onClick={() => { this.onHandleEmptySuffixToggle(info.dn) }}>
                                        Click here to create the top entry
                                    </a>
                                </div>
                            )
                    }]
                }
            ];
        }

        // For the tree view:
        const numSubValue = parseInt(info.numSubordinates);
        const randomId = generateUniqueId();
        // const nodeChildren = [];
        const nodeChildren = [{
            name: 'Loading...',
            id: randomId,
            icon: <Spinner size="sm" />,
            isFakeEntry: true
        }];

        const nodeDn = info.dn === '' ? 'Root DSE' : info.dn;
        const entryTreeData = {
            name: nodeDn,
            fullEntry: info.fullEntry, //
            children: numSubValue > 0 ? nodeChildren : null,
            modTime: info.modifyTimestamp,
            // The DN value can also be retrieved from info.fullEntry
            // ( but will require to retrieve the DN line and then split it. Taking a lazy approach here ;-))
            dn: info.dn,
            isEmptySuffix: isEmptySuffix,
            loadChildren: numSubValue > 0 // Used to insert child nodes when needed.
        };

        if (numSubValue > 0) {
            // Set the number of children.
            entryTreeData.customBadgeContent = numSubValue;
        }

        if (isEmptySuffix) {
            entryTreeData.icon = <ResourcesEmptyIcon color="var(--pf-global--palette--orange-300)" />;
        } else {
            entryTreeData.icon = null;
        }

        node_present = false;
        for (const node of this.rootSuffixesTreeData) {
            if (node.name === entryTreeData.name) {
                node.icon = entryTreeData.icon; // update icon
                node_present = true;
                break;
            }
        }
        if (!node_present) {
            this.rootSuffixesTreeData = [...this.rootSuffixesTreeData, entryTreeData];
        }

        if (this.rootSuffixesRows.length === 2 * this.state.numberSuffixes) { // There are 2 rows per DN.
            // Sort the row data to make sure that are ordered ( a child belong to its parent ).
            const finalRowData = this.rootSuffixesRows.sort((e1, e2) => e1.customRowId - e2.customRowId);
            this.setState({
                loading: false,
                rows: finalRowData,
                // Each row is composed of a parent and its single child.
                pagedRows: finalRowData.slice(0, 2 * this.state.perPage),
                total: finalRowData.length / 2,
                page: 1
            });

            const myTreeRootSuffixes = [...this.rootSuffixesTreeData];
            let myLocationIdentifier = 0;
            // Set the node ID:
            const treeViewRootSuffixes = myTreeRootSuffixes.map(node => {
                node.id = myLocationIdentifier.toString();
                myLocationIdentifier++;
                return node;
            });
            const timeOfCompletion = Date.now();
            this.setState({
                treeViewRootSuffixes,
                timeOfCompletion
            });
        } // TODO: If there was an error, the loading state would continue for ever.
        // Use better logic or render an empty table after a while ( 15 seconds? )
        // with some meaningful error message.
    }

    // Process the root suffixes
    processRootSuffixes = (userSuffData) => {
        let loading = this.state.loading;
        let isEmpty = false;
        // TODO: Use (!!userSuffData ...)
        if (userSuffData === undefined || userSuffData === null || userSuffData.length === 0) {
            // No suffixes, stop loading
            isEmpty = true;
            loading = false;
        }
        // Reset suffixes
        this.rootSuffixesRows = [];

        const suffixesToProcess = isEmpty ? [] : [...userSuffData];
        this.setState({
            numberSuffixes: suffixesToProcess.length,
            suffixList: [...userSuffData],
            loading
        }, () => {
            let parentId = 0;
            suffixesToProcess.map(suffixDN => {
                const params = {
                    serverId: this.props.serverId,
                    baseDn: suffixDN,
                    parentId: parentId,
                    addNotification: this.props.addNotification,
                };
                getRootSuffixEntryDetails(params, this.updateTableRootSuffixes);
                parentId += 2; // The next DN row will be two rows below.
            });
        });
    }

    showSuffixes = (label, event) => {
        getUserSuffixes(this.props.serverId, this.processRootSuffixes);
        if (!!label === false) {
            return;
        }

        const isRootSuff = (label === 'Database Suffixes');
        this.setState({
            onRootSuffixes: isRootSuff
        });

        const tableTitle = this.state.columns[0].title;
        // TODO ==> To optimize!
        if ((isRootSuff && (tableTitle !== 'Database Suffixes')) ||
            (!isRootSuff && (tableTitle === 'Database Suffixes'))) {
            const firstTitle = (label === 'Database Suffixes') ? 'Database Suffixes' : 'Entry DN';
            this.setState(prevState => ({
                columns: [
                    {
                        title: firstTitle,
                        cellFormatters: [expandable]
                    },
                    ...prevState.columns.slice(1)
                ]
            }), () => { this.setState({ searching: false }) });
        } else {
            this.setState({
                searching: false
            });
        }
    }

    actionResolver = (rowData, { rowIndex }) => {
        // No action on the children.
        if ((rowIndex % 2) === 1) {
            return null;
        }

        // If there is an entry suffix, allow only the creation of the top entry:
        if (rowData.isEmptySuffix) {
            return [{
                title: 'Create Top Entry...',
                onClick: () => {
                    this.onHandleEmptySuffixToggle(rowData.rawdn);
                }
            }];
        }

        let lockingDropdown = [];
        if (rowData.isLockable) {
            if (rowData.entryState !== "" && rowData.entryState !== "activated") {
                if (rowData.entryState.includes("probably activated") || rowData.entryState.includes("indirectly locked")) {
                    lockingDropdown = [{
                        title: 'Lock ...',
                        onClick:
                        () => {
                            const entryType = rowData.isRole ? "role" : "account";
                            this.setState({
                                entryDn: rowData.rawdn,
                                entryType: entryType,
                                operationType: "lock",
                                currentRowKey: rowData.secretTableRowKeyId
                            }, () => { this.handleConfirmModalToggle() });
                        }
                    }];
                } else {
                    lockingDropdown = [{
                        title: 'Unlock ...',
                        onClick:
                        () => {
                            const entryType = rowData.isRole ? "role" : "account";
                            this.setState({
                                entryDn: rowData.rawdn,
                                entryType: entryType,
                                operationType: "unlock",
                                currentRowKey: rowData.secretTableRowKeyId
                            }, () => { this.handleConfirmModalToggle() });
                        }
                    }];
                }
            } else if (rowData.entryState === "activated") {
                lockingDropdown = [{
                    title: 'Lock ...',
                    onClick:
                    () => {
                        const entryType = rowData.isRole ? "role" : "account";
                        this.setState({
                            entryDn: rowData.rawdn,
                            entryType: entryType,
                            operationType: "lock",
                            currentRowKey: rowData.secretTableRowKeyId
                        }, () => { this.handleConfirmModalToggle() });
                    }
                }];
            }
        }
        const keyIndex = this.state.keyIndex + 1;
        const updateActions =
            [{
                title: 'Search ...',
                onClick:
                () => {
                    this.setState({
                        activeTabKey: 2,
                        searchBase: rowData.rawdn
                    });
                }
            },
            {
                isSeparator: true
            },
            {
                title: 'Edit ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.edit,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true,
                        isTreeWizardOpen: false,
                    });
                }
            },
            {
                title: 'New ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.new,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true,
                        isTreeWizardOpen: false,
                    });
                }
            },
            {
                title: 'Rename ...',
                isDisabled: rowData.customRowId === 0, // can not rename root suffix
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.rename,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true,
                        isTreeWizardOpen: false,
                    });
                }
            },
            ...lockingDropdown,
            {
                title: 'ACIs ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.acis,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true,
                        isTreeWizardOpen: false,
                        keyIndex,
                    });
                }
            },
            {
                title: 'Class of Service ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.cos,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true,
                        isTreeWizardOpen: false,
                        keyIndex,
                    });
                }
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.delete,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true,
                        isTreeWizardOpen: false,
                    });
                }
            },
            {
                isSeparator: true
            },
            {
                title: 'Refresh ...',
                onClick:
                () => {
                    this.handleReload(true);
                }
            }];

        return [
            ...updateActions,
        ];
    }

    render () {
        const {
            page,
            perPage,
            total,
            columns,
            pagedRows,
            loading,
            navItems,
            entryMenuIsOpen,
            wizardOperationInfo,
            isWizardOpen,
            isTreeWizardOpen,
            wizardName,
            wizardEntryDn,
            treeViewRootSuffixes,
            refreshEntryTime
        } = this.state;

        return (
            <>
                {isWizardOpen && (
                    <GenericWizard
                        wizardName={wizardName}
                        isWizardOpen={isWizardOpen}
                        toggleOpenWizard={this.toggleOpenWizard}
                        wizardEntryDn={wizardEntryDn}
                        editorLdapServer={this.props.serverId}
                        treeViewRootSuffixes={this.state.treeViewRootSuffixes}
                        setWizardOperationInfo={this.setWizardOperationInfo}
                        onReload={this.handleReload}
                        onModrdnReload={this.handleReload}
                        allObjectclasses={this.state.allObjectclasses}
                        addNotification={this.props.addNotification}
                        key={this.state.keyIndex + "table"}
                    />
                )}
                {isTreeWizardOpen && (
                    <GenericWizard
                        wizardName={wizardName}
                        isWizardOpen={isTreeWizardOpen}
                        toggleOpenWizard={this.toggleOpenTreeWizard}
                        wizardEntryDn={wizardEntryDn}
                        editorLdapServer={this.props.serverId}
                        treeViewRootSuffixes={this.state.treeViewRootSuffixes}
                        setWizardOperationInfo={this.setWizardOperationInfo}
                        onReload={this.handleReloadNoop}
                        // coverity[copy_paste_error]
                        onModrdnReload={this.handleReload}
                        allObjectclasses={this.state.allObjectclasses}
                        addNotification={this.props.addNotification}
                        key={this.state.keyIndex + "tree"}
                    />
                )}
                <Tabs isBox className="ds-margin-top-lg ds-indent" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Tree View</TabTitleText>}>
                        <EditorTreeView
                            key={loading}
                            onToggleEntryMenu={this.handleToggleEntryMenu}
                            onSelectEntryOptions={this.handleSelectEntryOptions}
                            entryMenuIsOpen={entryMenuIsOpen}
                            treeViewRootSuffixes={treeViewRootSuffixes}
                            loading={loading}
                            timeOfCompletion={this.state.timeOfCompletion}
                            setTreeFirstClicked={this.setTreeFirstClicked}
                            editorLdapServer={this.props.serverId}
                            wizardOperationInfo={wizardOperationInfo}
                            refreshEntryTime={refreshEntryTime}
                            onReload={this.handleReload}
                            refreshing={this.state.refreshing}
                            allObjectclasses={this.state.allObjectclasses}
                            addNotification={this.props.addNotification}
                        />
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>Table View</TabTitleText>}>
                        <div className={this.state.searching ? "ds-disabled" : ""}>
                            <Breadcrumb className="ds-left-margin ds-margin-top-xlg">
                                {navItems.map(({ id, to, label, active }) => (
                                    <BreadcrumbItem key={id + label} to={to} isActive={active} onClick={() => this.onNavItemClick(id, active) }>
                                        {label}
                                    </BreadcrumbItem>
                                ))}
                            </Breadcrumb>
                            <FontAwesomeIcon
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title="Refresh"
                                onClick={this.handleReload}
                            />
                        </div>
                        <div className={this.state.searching ? "ds-margin-top-xlg ds-center" : "ds-hidden"}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Loading <i>{this.state.searchBase}</i> ...
                                </Text>
                            </TextContent>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>
                        <div className={this.state.searching ? "ds-hidden" : ""}>
                            <EditorTableView
                                key={loading}
                                loading={loading}
                                // Pagination
                                itemCount={total}
                                page={page}
                                perPage={perPage}
                                onSetPage={(value) => this.getPageData(value, perPage)}
                                onPerPageSelect={(value) => this.getPageData(1, value)}
                                // Table
                                editorTableRows={pagedRows}
                                onCollapse={this.handleCollapse}
                                columns={columns}
                                actionResolver={this.actionResolver}
                                addNotification={this.props.addNotification}
                            />
                        </div>
                    </Tab>
                    <Tab eventKey={2} title={<TabTitleText>Search</TabTitleText>}>
                        <SearchDatabase
                            key={this.state.suffixList + this.state.searchBase}
                            serverId={this.props.serverId}
                            suffixList={this.state.suffixList}
                            attributes={this.state.attributes}
                            searchBase={this.state.searchBase}
                            allObjectclasses={this.state.allObjectclasses}
                            addNotification={this.props.addNotification}
                        />
                    </Tab>
                </Tabs>
                {this.state.showEmptySuffixModal &&
                    <CreateRootSuffix
                        showEmptySuffixModal={this.state.showEmptySuffixModal}
                        handleEmptySuffixToggle={this.onHandleEmptySuffixToggle}
                        suffixDn={this.state.emptyDN}
                        editorLdapServer={this.props.serverId}
                    />}
                <Modal
                    // TODO: Fix confirmation modal formatting and size; add operation to the tables
                    variant={ModalVariant.medium}
                    title={
                        `Are you sure you want to ${this.state.operationType} the ${this.state.entryType}?`
                    }
                    isOpen={this.state.isConfirmModalOpen}
                    onClose={this.handleConfirmModalToggle}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={this.handleLockUnlockEntry}>
                            Confirm
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleConfirmModalToggle}>
                            Cancel
                        </Button>
                    ]}
                >
                    <TextContent className="ds-margin-top ds-hide-vertical-scrollbar">
                        <Text>
                            {this.state.entryType === "account"
                                ? `It will ${this.state.operationType === "lock" ? "add" : "remove"} nsAccountLock attribute
                            ${this.state.operationType === "lock" ? "to" : "from"} the entry - ${this.state.entryDn}.`
                                : `This operation will make sure that these five entries are created at the entry's root suffix (if not, they will be created):`}
                        </Text>
                        {this.state.entryType === "role" &&
                        <>
                            <TextList>
                                <TextListItem>
                                    cn=nsManagedDisabledRole
                                </TextListItem>
                                <TextListItem>
                                    cn=nsDisabledRole
                                </TextListItem>
                                <TextListItem>
                                    cn=nsAccountInactivationTmp (with a child)
                                </TextListItem>
                                <TextListItem>
                                    cn=nsAccountInactivation_cos
                                </TextListItem>
                            </TextList>
                            <Text>
                                {`The entry - ${this.state.entryDn} - will be ${this.state.operationType === "lock" ? "added to" : "removed from"} nsRoleDN attribute in cn=nsDisabledRole entry in the root suffix.`}
                            </Text>
                        </>}
                    </TextContent>
                </Modal>
            </>
        );
    }
}
