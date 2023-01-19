import React from 'react';
import {
    Bullseye,
    Spinner,
    TreeView
} from '@patternfly/react-core';
import {
    ExclamationTriangleIcon,
    FolderIcon,
    FolderOpenIcon,
    ResourcesEmptyIcon
} from '@patternfly/react-icons';
import {
    getBaseLevelEntryAttributes,
    getOneLevelEntries,
    getRootSuffixEntryDetails,
    runGenericSearch,
    ldapPing
} from './utils.jsx';
import PropTypes from "prop-types";

class LdapNavigator extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            // Need to use an initial object to avoid a JS exception.
            allItems: [{
                name: 'Loading LDAP entries...',
                id: 'Loading',
                icon: <Spinner size="sm"/>
            }],
            activeItems: [],
            ldapFailure: false
        };

        this.treeOnClick = (evt, treeViewItem, parentItem) => {
            if (treeViewItem.isFakeEntry) {
                return;
            }

            if (this.state.activeItems.length > 0 && treeViewItem.dn === this.state.activeItems[0].dn) {
                // node was already clicked, just return
                return;
            }

            this.setState({
                activeItems: [treeViewItem, parentItem]
                // updatingTree: true
            },
            () => {
                if (typeof this.props.showTreeLoadingState === 'function') {
                    this.props.showTreeLoadingState(true);
                }
                this.refreshNode(treeViewItem, true);
            });
        };

        this.refreshNode = (treeViewItem, forceReloadChildren) => {
            getBaseLevelEntryAttributes(this.props.editorLdapServer,
                treeViewItem.dn,
                (entryDetails) => {
                    // TODO: No need to set the 'fullEntry' property when only navigating ( when called from the ACI Wizard for instance ).
                    treeViewItem.fullEntry = [...entryDetails];
                    // For root suffixes, check if they are empty.
                    if ((treeViewItem.id.indexOf('.') === -1) && // No dot in the id ==> Entry is a root node in the tree.
                        (!this.state.ldapFailure)) {
                        // A suffix is empty if the LDAP server is up
                        // and there is no existing root entry.
                        const entryIsAnEmptySuffix = entryDetails[0].errorCode === 32;
                        treeViewItem.isEmptySuffix = entryIsAnEmptySuffix;
                        if (entryIsAnEmptySuffix) {
                            this.props.handleNodeOnClick(treeViewItem);
                            this.props.showTreeLoadingState(false);
                            return;
                        }
                    }

                    // Update the entry details table.
                    if (!this.state.ldapFailure) {
                        this.props.handleNodeOnClick(treeViewItem);
                    }

                    if ((treeViewItem.loadChildren) || // Load child entries if any
                        (forceReloadChildren)) { // or if requested to load.
                        const params = {
                            serverId: this.props.editorLdapServer,
                            baseDn: treeViewItem.dn,
                            name: treeViewItem.name,
                            fullEntry: treeViewItem.fullEntry,
                            modTime: treeViewItem.modTime,
                            addNotification: this.props.addNotification,
                            filter: this.props.skipLeafEntries
                                ? '(|(&(numSubordinates=*)(numSubordinates>=1))(objectClass=organizationalunit)(objectClass=organization))'
                                : null // getOneLevelEntries() will use its default filter '(|(objectClass=*)(objectClass=ldapSubEntry))'
                        };

                        if (this.props.skipLeafEntries) {
                            // updateDirectChildren() will call processDirectChildren() with the updated children array.
                            getOneLevelEntries(params, this.updateDirectChildren);
                        } else {
                            getOneLevelEntries(params, this.processDirectChildren);
                        }
                    } else {
                        // No children to load so the tree will not be updated.
                        // this.setState({ updatingTree: false });
                        this.props.showTreeLoadingState(false);
                    }
                });
        };

        this.updateMyParent = (treeNode, nodeIdObject, nodeChildren, removePreviousChildren) => {
            const res = nodeIdObject.remainingId.indexOf('.');
            if (res === -1) {
                const insertionNode = treeNode.find(elt => elt.id === nodeIdObject.fullId);
                insertionNode.children = nodeChildren;
                insertionNode.loadChildren = false;
                insertionNode.customBadgeContent = "?";
            } else {
                const parentId = nodeIdObject.remainingId.substring(0, res);
                const startingId = nodeIdObject.startingId === undefined
                    ? parentId
                    : `${nodeIdObject.startingId}.${parentId}`;

                const parentNode = treeNode.find(elt => elt.id === startingId);
                const remainingId = nodeIdObject.remainingId.substring(res + 1);

                const newNodeIdObject = {
                    fullId: nodeIdObject.fullId,
                    remainingId: remainingId,
                    startingId: startingId
                }
                this.updateMyParent(parentNode.children, newNodeIdObject, nodeChildren, removePreviousChildren);
            }
        };

        this.updateMyChildren = (treeNode, nodeIdObject, childArray, removePreviousChildren) => {
            const res = nodeIdObject.remainingId.indexOf('.');
            if (res === -1) {
                const insertionNode = treeNode.find(elt => elt.id === nodeIdObject.fullId);
                // const myId = parseInt(nodeIdObject.remainingId);
                if (!insertionNode) {
                    return;
                }
                const childrenInfo = insertionNode.children;
                const myCurrentChildren = removePreviousChildren || (!childrenInfo)
                    ? []
                    : childrenInfo;
                if ((childArray.length > 0) || (myCurrentChildren.length > 0)) {
                    // Add the childArray data first so the new children
                    // are inserted close to the parent node.
                    // User will not need to scroll down to see the new children.
                    const myFinalChildren = [...childArray, ...myCurrentChildren];
                    // Find the parent node by its full ID.
                    insertionNode.children = myFinalChildren;
                    insertionNode.customBadgeContent = myFinalChildren.length;

                    // In case the property was set ( see the hack in the "else" statement below. )
                    // The rendering use the default value.
                    delete insertionNode.expandedIcon;
                } else {
                    insertionNode.children = null;
                    insertionNode.action = null;
                    // Hack to fix the case where the node stays with the expandedIcon after
                    // deleting all the children.
                    insertionNode.expandedIcon = <FolderIcon />;
                }

                // Don't search for children again ( unless there is a user-initiated refresh ).
                insertionNode.loadChildren = false;
            } else {
                const parentId = nodeIdObject.remainingId.substring(0, res);
                const startingId = nodeIdObject.startingId === undefined
                    ? parentId
                    : `${nodeIdObject.startingId}.${parentId}`;

                // console.log(`parentId=${parentId}`);
                const parentNode = treeNode.find(elt => elt.id === startingId);
                const remainingId = nodeIdObject.remainingId.substring(res + 1);

                const newNodeIdObject = {
                    fullId: nodeIdObject.fullId,
                    remainingId: remainingId,
                    startingId: startingId
                }
                // console.log(newNodeIdObject);
                // this.updateMyChildren(treeNode[parentId].children, newNodeIdObject, childArray, removePreviousChildren);
                this.updateMyChildren(parentNode.children, newNodeIdObject, childArray, removePreviousChildren);
            }
        }

        // Update a root suffix node.
        this.updateRootSuffixNode = (isAdded) => {
            const currentActiveNode = this.state.activeItems[0];
            const rootSuffId = currentActiveNode.id;
            const treeAllItems = this.state.allItems;
            const rootSuffixNode = treeAllItems.find(elt => elt.id === rootSuffId);
            if (isAdded) { // Adding a root suffix.
                const newItem = Object.assign({}, this.props.newSuffixData.rootEntryData);
                newItem.id = rootSuffId;
                newItem.isEmptySuffix = false;
                newItem.loadChildren = false;
                treeAllItems.splice(rootSuffId, 1, newItem);
                this.setState({ allItems: treeAllItems });
                // Update the entry table.
                this.props.updateEntryRows(newItem);
            } else { // An existing root suffix was deleted.
                const emptyItem = {
                    dn: rootSuffixNode.dn,
                    id: rootSuffId,
                    children: null,
                    isEmptySuffix: true,
                    icon: <ResourcesEmptyIcon color="var(--pf-global--palette--orange-300)" />,
                    name: rootSuffixNode.name,
                    fullEntry: []
                }

                treeAllItems.splice(rootSuffId, 1, emptyItem);
                this.setState({ allItems: treeAllItems });
                // Update the entry table.
                this.props.updateEntryRows(emptyItem);
            }
        }
    } // End constructor.

    componentDidMount () {
        // This is fine since the first entries are root suffixes
        // so the tree is not yet nested.
        this.setState({
            allItems: [...this.props.treeItems],
        });
    }

    componentDidUpdate (prevProps) {
        // Get the tree initial data.
        if (this.props.timeOfCompletion !== prevProps.timeOfCompletion) {
            this.setState({ allItems: [...this.props.treeItems] });
        }

        // Update the tree data after a suffix creation.
        if ((this.props.newSuffixData !== undefined) &&
        this.props.newSuffixData.creationDate > prevProps.newSuffixData.creationDate) {
            this.updateRootSuffixNode(true); // true ==> the suffix top entry is being created.
        }

        // Update the tree data after an LDAP update.
        if ((this.props.wizardOperationInfo !== undefined) &&
        this.props.wizardOperationInfo.time !== prevProps.wizardOperationInfo.time) {
            this.updateTreeData();
        }

        // Refresh the selected tree node.
        if ((this.props.refreshEntryTime !== prevProps.refreshEntryTime) ||
            (this.props.refreshButtonTriggerTime !== prevProps.refreshButtonTriggerTime)) {
            // Second parameter ( true ) will force the reload of the children.
            this.refreshNode(this.state.activeItems[0], true);
        }
    }

    updateTreeData = () => {
        // Stop if there was an LDAP error. In that case the tree should not be updated.
        if (this.props.wizardOperationInfo.resultCode !== 0) {
            const msg = `There was an LDAP error (code ${this.props.wizardOperationInfo.resultCode}). ` +
            'Not updating the LDAP tree.'
            console.log(msg);
            return;
        }

        if (this.state.activeItems.length === 0) {
            return;
        }

        switch (this.props.wizardOperationInfo.operationType) {
            case 'DELETE': {
                const currentActiveNode = this.state.activeItems[0];
                const nodeId = currentActiveNode.id;

                if (nodeId.indexOf('.') === -1) { // No dot in the id ==> Entry is a root node in the tree.
                    // Update the tree data after a suffix deletion.
                    this.updateRootSuffixNode(false);
                } else {
                    // Handle non-suffix deletion.
                    // console.log('Normal entry deletion');
                    const treeAllItems = this.state.allItems;
                    const parentNode = this.state.activeItems[1];
                    const parentId = parentNode.id;
                    // const parentDn = parentNode.dn;
                    const parentChildren = [...parentNode.children];
                    const nodeIndex = parentChildren.findIndex(datum => datum.id === nodeId);
                    console.log(`nodeIndex = ${nodeIndex}`);

                    if (nodeIndex >= 0) { // Should always be the case since the node is visible. TODO: Use an assertion.
                        parentChildren.splice(nodeIndex, 1); // Remove the deleted child.
                        // 4th parameter set at true to remove the node and its children ( if any ).
                        const parentIdObject = {
                            fullId: parentId,
                            remainingId: parentId
                        }
                        this.updateMyChildren(treeAllItems, parentIdObject, parentChildren, true);
                        const activeItems = this.state.activeItems.shift();
                        this.setState({
                            allItems: treeAllItems,
                            // Unset the current active items.
                            // If there is an active item, it will give the false perception that the
                            // entry details ( empty ) table is showing its information.
                            activeItems
                        },
                        () => {
                            this.props.handleNodeOnClick(null);
                        });
                    }
                }
                break;
            }
            case 'ADD':
            {
                const nodeDn = this.props.wizardOperationInfo.entryDn;
                const relativeDn = this.props.wizardOperationInfo.relativeDn;
                const myFutureParent = this.state.activeItems[0]; // The future parent is the current node.
                const parentId = myFutureParent.id;
                const idArray = myFutureParent.children
                    ? myFutureParent.children.map(elt => elt.id.split('.').pop())
                    : [];
                const maxId = idArray.length > 0
                    ? Math.max(...idArray)
                    : 0;
                const mySubId = maxId + 1;
                const params = {
                    serverId: this.props.editorLdapServer,
                    baseDn: nodeDn,
                    addNotification: this.props.addNotification,
                };

                // TODO: Change the name of this function to a more generic one!!
                getRootSuffixEntryDetails(params,
                    (result) => {
                        const info = JSON.parse(result);
                        const entryTreeData =
                        {
                            name: relativeDn,
                            fullEntry: info.fullEntry,
                            modTime: info.modifyTimestamp,
                            dn: info.dn,
                            id: `${parentId}.${mySubId}`,
                            loadChildren: false
                        };

                        const treeAllItems = this.state.allItems;
                        const parentIdObject = {
                            fullId: parentId,
                            remainingId: parentId
                        }
                        this.updateMyChildren(treeAllItems, parentIdObject, [entryTreeData]);
                        this.setState({ allItems: treeAllItems });
                        // this.props.handleNodeOnClick(entryTreeData);
                    });

                break;
            }

            case 'MODRDN':
            case 'MODIFY':
                this.refreshNode(this.state.activeItems[0]);
                break;

            default:
                console.log(`Unknown operation type in LdapNavigator class: ${this.props.wizardOperationInfo.operationType}`);
        }
    }

    updateDirectChildren = (potentialChildren, params, resCode) => {
        // When leaf entries ( but Organizations and Organization Units ) should be skipped
        // run a search with the relevant filter to get the actual number of matching grand children
        // ( children of the direct children of the current entry [ the active node ]).

        if (!this.props.skipLeafEntries) {
            return;
        }

        if (potentialChildren === null) {
            if (resCode.exit_status !== 0) {
                this.props.addNotification(
                    "error",
                    `Error searching database - ${resCode.msg.split("\n").pop()}`
                );
            }
            return;
        }

        let updatedChildren = [];
        let nbIterations = 0;

        const child_params = {
            serverId: this.props.editorLdapServer,
            scope: 'one',
            attributes: '1.1',
            addNotification: this.props.addNotification,
            filter: '(|(&(numSubordinates=*)(numSubordinates>=1))(objectClass=organizationalunit)(objectClass=organization))'
        };

        potentialChildren.map(aChild => {
            const info = JSON.parse(aChild);
            params.baseDn = info.dn;
            runGenericSearch(child_params, (resArray) => {
                if (resArray.length > 0) {
                    info.showChildren = true;
                }
                // TODO: Get rid of the JSON stringify / parse. Just use plain JS objects!
                updatedChildren.push(JSON.stringify(info));
                nbIterations++;
                if (nbIterations === potentialChildren.length) {
                    // Now process the selected direct children.
                    this.processDirectChildren(updatedChildren, params, null);
                }
            });
        });
    }

    // Process the entries that are direct children.
    processDirectChildren = (directChildren, params, resCode) => {
        // Retrieve the selected node from ==> this.state.activeItems: [treeViewItem, parentItem]
        const myActiveNode = this.state.activeItems[0];
        let myChildren = [];
        let childId = 0; // Used to quickly locate the node in the tree data.

        if (directChildren === null) { // There was a failure to connect to the LDAP server.
            this.setState({ ldapFailure: true });
            this.props.showTreeLoadingState(false);

            if (resCode.exit_status !== 0) {
                this.props.addNotification(
                    "error",
                    `Error searching database - ${resCode.msg.split("\n").pop()}`
                );

                const randomId = Math.random().toString(36).substring(2, 15);
                let nodeChildren = [{
                    name: 'Encountered an error, unable to display child entries',
                    id: randomId,
                    icon: <ExclamationTriangleIcon />,
                    isFakeEntry: true
                }];

                const treeAllItems = this.state.allItems;
                const parentIdObject = {
                    fullId: myActiveNode.id,
                    remainingId: myActiveNode.id
                }
                this.updateMyParent(treeAllItems, parentIdObject, nodeChildren, true);
                this.setState({
                    allItems: treeAllItems
                });
            }
            return;
        } else {
            // TODO: To optimize!
            // ==> Set the state only if needed.
            this.setState({ ldapFailure: false });
        }

        for (const aChild of directChildren) {
            const info = JSON.parse(aChild);
            const numSubValue = parseInt(info.numSubordinates);
            let nodeChildren = null;
            // The property showChildren is added in the function updateDirectChildren().
            const showChildren = !this.props.skipLeafEntries || info.showChildren;
            if ((numSubValue > 0) && showChildren) {
                const randomId = Math.random().toString(36).substring(2, 15);
                nodeChildren = [{
                    name: 'Loading...',
                    id: randomId,
                    // icon: <UnknownIcon />,
                    icon: <Spinner size="sm"/>,
                    isFakeEntry: true
                }];
            }

            // When called by the ACI Wizard, leaf entries are not needed.
            // Property 'skipLeafEntries' is set to 'true' in Wizard AddNewAci.

            const childData = {
                // TODO: Use a better logic to retrieve the entry RDN!!!
                name: info.dn.slice(0, info.dn.length - myActiveNode.dn.length - 1),
                fullEntry: info.fullEntry,
                // IMPORTANT: Need to add the children so the tree shows the expansion button!
                children: nodeChildren,
                modTime: info.modifyTimestamp,
                dn: info.dn,
                id: `${myActiveNode.id}.${childId}`,
                loadChildren: numSubValue > 0 // Used to insert child nodes when needed.
            };

            if (numSubValue > 0) {
                // Set the number of children.
                childData.customBadgeContent = numSubValue;
            }

            myChildren.push(childData);
            childId++;
        }

        if (directChildren.length > 0) {
            const treeAllItems = this.state.allItems;
            const parentIdObject = {
                fullId: myActiveNode.id,
                remainingId: myActiveNode.id
            }
            // this.updateMyChildren(treeAllItems, myActiveNode.id, myChildren, true);
            this.updateMyChildren(treeAllItems, parentIdObject, myChildren, true);
            this.setState({ allItems: treeAllItems });
        }

        this.props.showTreeLoadingState(false);
    } // End processDirectChildren().

    render () {
        const { allItems, activeItems } = this.state;
        return (
            <React.Fragment>
                {allItems.length === 0 &&
                    <Bullseye>
                        <div>No Databases</div>
                    </Bullseye>
                }
                <div className={this.props.isDisabled ? "ds-disabled ds-editor-tree" : "ds-editor-tree"}>
                    <TreeView
                        data={allItems}
                        onSelect={this.treeOnClick}
                        activeItems={activeItems}
                        icon={this.props.skipLeafEntries ? null : <FolderIcon />}
                        expandedIcon={this.props.skipLeafEntries ? null : <FolderOpenIcon />}
                        hasBadges={!this.props.skipLeafEntries}
                    />
                </div>
            </React.Fragment>
        );
    }
}

LdapNavigator.propTypes = {
    editorLdapServer: PropTypes.string,
    treeItems: PropTypes.array,
    skipLeafEntries: PropTypes.bool,
    showTreeLoadingState: PropTypes.func,
    handleNodeOnClick: PropTypes.func,
};

LdapNavigator.defaultProps = {
    editorLdapServer: "",
    treeItems: [],
    skipLeafEntries: false,
};

export default LdapNavigator;
