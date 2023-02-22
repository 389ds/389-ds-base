import React from 'react';
import {
    Alert,
    Bullseye,
    Button,
    Card, CardBody, CardTitle,
    Checkbox,
    Divider,
    Drawer, DrawerPanelContent, DrawerContent,
    DrawerContentBody, DrawerHead,
    DrawerActions, DrawerCloseButton,
    DualListSelector,
    Form, FormHelperText, FormSelect, FormSelectOption,
    Grid, GridItem,
    HelperText, HelperTextItem,
    Modal, ModalVariant,
    SearchInput,
    Select, SelectOption, SelectVariant,
    Spinner,
    Text, TextArea, TextContent, TextInput, TextVariants,
    TimePicker,
    Title,
    Tooltip,
    ValidatedOptions,
    Wizard,
} from '@patternfly/react-core';
import {
    Table,
    TableHeader,
    TableBody,
    sortable,
    SortByDirection
} from '@patternfly/react-table';
import {
    runGenericSearch, getSearchEntries, getAttributesNameAndOid,
    getRdnInfo, isValidIpAddress, isValidLDAPUrl, isValidHostname,
    modifyLdapEntry
} from '../../lib/utils.jsx';
import {
    valid_filter
} from '../../../tools.jsx';
import GenericPagination from '../../lib/genericPagination.jsx';
import LdapNavigator from '../../lib/ldapNavigator.jsx';
import AciBindRuleTable from "./aciBindRuleTable.jsx";
import PlusCircleIcon from '@patternfly/react-icons/dist/js/icons/plus-circle-icon';
import SearchIcon from '@patternfly/react-icons/dist/js/icons/search-icon';
import InfoCircleIcon from '@patternfly/react-icons/dist/js/icons/info-circle-icon';
import ArrowRightIcon from '@patternfly/react-icons/dist/js/icons/arrow-right-icon';
import TimesIcon from '@patternfly/react-icons/dist/js/icons/times-icon';

class AddNewAci extends React.Component {
    constructor (props) {
        super(props);

        this.rightsColumns = [{ title: 'Right' }, { title: 'Description' }];
        this.targetsColumns = [
            { title: 'Name', transforms: [sortable] },
            { title: 'OID', transforms: [sortable] }
        ];
        this.dayMap = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'];

        this.state = {
            // General
            editVisually: true,
            stepIdReachedVisual: 1,
            savedStepId: 1,
            isAciSyntaxValid: false,
            searchPattern: '',
            newAciName: '',
            isTreeLoading: false,
            searching: false,
            aciText: "",
            aciTextNew: "",
            attributes: [],
            resultVariant: "success",
            commandOutput: "",
            // targets
            target: this.props.wizardEntryDn,
            targetAttrs: [],
            targetAttrCompOp: "=",
            isOpenTargetAttrOperator: false,
            targetFilter: "",
            target_to: "",
            target_from: "",
            // Bind rules
            specialSelection: "",
            bindRuleRows: [],
            haveUserRules: false,
            haveUserAttrRules: false,
            userattrOperator: "=",
            userattrAttr: "",
            userattrBindType: "USERDN",
            userAttrParent0: false,
            userAttrParent1: false,
            userAttrParent2: false,
            userAttrParent3: false,
            userAttrParent4: false,
            authmethod: "none",
            authMethods: [],
            authMethodOperator: "=",
            hosts: [],
            ips: [],
            dayofweek: [],
            sunday: false,
            monday: false,
            tuesday: false,
            wednesday: false,
            thursday: false,
            friday: false,
            saturday: false,
            timeOfDayStart: "0000",
            timeStartCompOp: ">",
            timeOfDayEnd: "0000",
            timeEndCompOp: "<",
            ssf: "",
            ssfOperator: ">=",
            // Users
            usersSearchBaseDn: this.props.wizardEntryDn,
            isSearchRunning: false,
            usersAvailableOptions: [],
            usersChosenOptions: [],
            isUsersDrawerExpanded: false,
            isTargetsDrawerExpanded: false,
            // Rights
            rightType: "allow",
            isOpenRights: false,
            rightsRows: [
                { cells: ['read', 'See the values of targeted attributes'], selected: false },
                { cells: ['compare', 'Compare targeted attribute values'], selected: false },
                { cells: ['search', 'Determine if targeted attributes exist'], selected: false },
                { cells: ['selfwrite', 'Add one\'s own DN to the target'], selected: false },
                { cells: ['write', 'Modify targeted attributes'], selected: false },
                { cells: ['delete', 'Remove targeted entries'], selected: false },
                { cells: ['add', 'Add targeted entries'], selected: false },
                { cells: ['moddn', 'Move an entry from one subtree to another'], selected: false },
                { cells: ['proxy', 'Authenticate as another user'], selected: false }
            ],
            // Targets
            targetAttrRows: [],
            sortBy: {},
            tableModificationTime: 0,
            // Add bind rule modal
            showAddBindRuleModal: false,
            bindRuleType: "userdn",
            bindRuleOperator: "=",
            ldapURL: "",
            adding: true,
        };

        this.getUserattrVal = () => {
            const {
                userAttrParent0, userAttrParent1, userAttrParent2,
                userAttrParent3, userAttrParent4, userattrAttr,
                userattrOperator, userattrBindType
            } = this.state;

            let inheritVal = "";
            if (userAttrParent1 || userAttrParent2 || userAttrParent3 || userAttrParent4) {
                inheritVal = "parent[0";
                if (userAttrParent1) {
                    inheritVal += ",1";
                }
                if (userAttrParent2) {
                    inheritVal += ",2";
                }
                if (userAttrParent3) {
                    inheritVal += ",3";
                }
                if (userAttrParent4) {
                    inheritVal += ",4";
                }
                inheritVal += "].";
            }
            inheritVal += userattrAttr + "#" + userattrBindType;
            return inheritVal;
        }

        this.buildAciText = () => {
            const {
                bindRuleRows,
                dayofweek,
                newAciName,
                rightsRows, rightType,
                target_to, target_from,
                target,
                targetAttrs, targetAttrCompOp,
                targetFilter,
                timeOfDayStart, timeStartCompOp,
                timeOfDayEnd, timeEndCompOp,
                sunday, monday, tuesday, wednesday, thursday, friday, saturday,
                userAttrParent0, userAttrParent1, userAttrParent2,
                userAttrParent3, userAttrParent4,
            } = this.state;

            if (target == "") {
                this.setState({
                    aciText: "??????",
                });
                return;
            }

            // Process bind rules
            let userDNs = [];
            let groupDNs = [];
            let roleDNs = [];
            let authMethods = [];
            let ssfs = [];
            let ips = [];
            let dns = [];
            let userattr = [];
            for (const rule of bindRuleRows) {
                if (rule.cells[0] === 'userdn') {
                    userDNs.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                } else if (rule.cells[0] === 'groupdn') {
                    groupDNs.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                } else if (rule.cells[0] === 'roledn') {
                    roleDNs.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                }  else if (rule.cells[0] === 'authmethod') {
                    authMethods.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                } else if (rule.cells[0] === 'ssf') {
                    ssfs.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                } else if (rule.cells[0] === 'ip') {
                    ips.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                } else if (rule.cells[0] === 'dns') {
                    dns.push(rule.cells[0] + rule.cells[1] + '"' + rule.cells[2] + '"');
                } else if (rule.cells[0] === 'userattr') {
                    userattr.push(rule.cells[0] + rule.cells[1] + '"' + this.getUserattrVal() + '"');
                }
            }

            let aciText = '(target="ldap:///' + target + '")';

            // target attrs
            if (targetAttrs.length > 0) {
                aciText += '(targetattr' + targetAttrCompOp + '"';
                for (const attrIdx in targetAttrs) {
                    aciText += targetAttrs[attrIdx];
                    if (targetAttrs.length > 1 && (Number(attrIdx) + 1) < targetAttrs.length) {
                        aciText += " || ";
                    }
                }
                aciText += '")';
            } else {
                aciText += '(targetattr="*")';
            }

            // target filter
            if (targetFilter !== "") {
                aciText += '(targetfilter="' + targetFilter + '")';
            }

            // target from and to
            if (target_from !== "" && target_to !== "") {
                aciText += '(target_from="' + target_from + '")';
                aciText += '(target_to="' + target_to + '")';
            }

            // middle section of ACI
            aciText += '(version 3.0; acl "' + newAciName + '"; ';

            // Rights
            const rights = rightsRows
                .filter(item => item.selected)
                .map(cells => cells.cells[0])
                .toString();
            aciText += rightType + "(" + rights + ")";

            let brCount = 0;

            // Bind rules
            for (const bindRule of [userDNs, groupDNs, roleDNs, authMethods, ssfs, ips, dns, userattr]) {
                if (bindRule.length > 0) {
                    let many = bindRule.length > 1;
                    if (brCount > 0) {
                        aciText += ' and '
                    } else {
                        aciText += ' ';
                    }
                    if (many) {
                        // Need to group under a single ()
                        aciText += "(";
                    }
                    for (const idx in bindRule) {
                        aciText += bindRule[idx];
                        if (many && idx < bindRule.length) {
                            aciText += " or ";
                        }
                    }
                    if (many) {
                        // Need to group rules under single ()
                        aciText += ")";
                    }
                    brCount += 1;
                }
            }

            // Days of week
            if (sunday || monday || tuesday || wednesday || thursday || friday || saturday) {
                if (brCount > 0) {
                    aciText += ' and '
                } else {
                    aciText += ' ';
                }
                const allDays = [sunday, monday, tuesday, wednesday, thursday, friday, saturday]
                let days = [];
                aciText += 'dayofweek="';
                for (const idx in allDays) {
                    if (allDays[idx]) {
                        days.push(this.dayMap[idx]);
                    }
                }
                aciText += days.join(',') + '"';
                brCount += 1;
            }
            // times
            if (timeOfDayStart !== "0000" || timeOfDayEnd !== "0000") {
                if (brCount > 0) {
                    aciText += ' and '
                } else {
                    aciText += ' ';
                }
                aciText += '(timeofday' + timeStartCompOp + '"' + timeOfDayStart + '"';
                aciText += ' and timeofday' + timeEndCompOp + '"' + timeOfDayEnd + '")';
                brCount += 1;
            }

            // The end
            aciText += ";)";

            this.setState({
                aciText: aciText,
                aciTextNew: aciText,
            })
        };

        this.handleSelectedAttrs = (attrs) => {
            this.setState({
                targetAttrs: attrs
            });
        };

        // TODO: Define a generic Drawer component that shows the LDAP tree
        // rather than defining everything twice!!!
        this.usersDrawerRef = React.createRef();
        this.targetsDrawerRef = React.createRef();

        // Target attr operator
        this.onToggleTargetAttrOp = isOpenTargetAttrOperator => {
            this.setState({
                isOpenTargetAttrOperator
            });
        };
        this.onSelectTargetAttrOp = (event, selection) => {
            this.setState({
                targetAttrCompOp: selection,
                isOpenTargetAttrOperator: false,
            });
        };

        // rights
        this.onToggleRights = isOpenRights => {
            this.setState({
                isOpenRights
            });
        }
        this.onSelectRights = (event, selection) => {
            this.setState({
                rightType: selection,
                isOpenRights: false,
            });
        }

        this.onUsersDrawerExpand = () => {
            this.usersDrawerRef.current && this.usersDrawerRef.current.focus();
        };

        this.onTargetsDrawerExpand = () => {
            this.targetsDrawerRef.current && this.targetsDrawerRef.current.focus();
        };

        this.onUsersDrawerClick = () => {
            const isUsersDrawerExpanded = !this.state.isUsersDrawerExpanded;
            this.setState({
                isUsersDrawerExpanded
            });
        };

        this.onTargetsDrawerClick = () => {
            const isTargetsDrawerExpanded = !this.state.isTargetsDrawerExpanded;
            this.setState({
                isTargetsDrawerExpanded
            });
        };

        this.onUsersDrawerCloseClick = () => {
            this.setState({
                isUsersDrawerExpanded: false
            });
        };

        this.onTargetsDrawerCloseClick = () => {
            this.setState({
                isTargetsDrawerExpanded: false
            });
        };

        this.handleSearchClick = () => {
            if (this.state.isSearchRunning) {
                return;
            }
            this.setState({
                isSearchRunning: true,
                usersAvailableOptions: []
            }, this.getEntries);
        };

        this.handleTimeChange = (time_str, id) => {
            const time_val = time_str.replace(":", "");

            this.setState({
                [id]: time_val,
            });
        }

        this.getEntries = () => {
            const searchArea = this.state.bindRuleType;
            const baseDn = this.state.usersSearchBaseDn;
            let filter = '';
            const pattern = this.state.searchPattern;

            if (searchArea === 'userdn') {
                filter = pattern === ''
                    ? '(|(objectClass=person)(objectClass=nsPerson)(objectClass=nsAccount)(objectClass=nsOrgPerson)(objectClass=posixAccount))'
                    : `(&(|(objectClass=person)(objectClass=nsPerson)(objectClass=nsAccount)(objectClass=nsOrgPerson)(objectClass=posixAccount))(|(cn=*${pattern}*)(uid=${pattern})))`;
            } else if (searchArea === 'groupdn') {
                filter = pattern === ''
                    ? '(|(objectClass=groupofuniquenames)(objectClass=groupofnames))'
                    : `(&(|(objectClass=groupofuniquenames)(objectClass=groupofnames))(cn=*${pattern}*))`;
            } else if (searchArea === 'roledn') {
                filter = pattern === ''
                    ? '(&(objectClass=ldapsubentry)(objectClass=nsRoleDefinition))'
                    : `(&(objectClass=ldapsubentry)(objectClass=nsRoleDefinition)(cn=*${pattern}*))`;
            }

            let params = {
                serverId: this.props.editorLdapServer,
                searchBase: baseDn,
                searchFilter: filter,
                searchScope: 'sub',
                sizeLimit: 2000,
                timeLimit: 5,
                addNotification: this.props.addNotification,
            };

            let searchResults = [];

            getSearchEntries(params, (resultArray) => {
                let results = resultArray.map(result => {
                    const info = JSON.parse(result);
                    return (info.dn);
                });
                results.sort();
                const newOptionsArray = results.map(entryDN => {
                    const rdnInfo = getRdnInfo(entryDN);
                    return (
                        <span title={entryDN}>
                            {rdnInfo.rdnVal}
                        </span>
                    );
                });

                this.setState({
                    usersAvailableOptions: newOptionsArray,
                    isSearchRunning: false
                });
            });
        }

        // Rights:
        this.rightsOnSelect = (event, isSelected, rowId) => {
            let rows;
            if (rowId === -1) {
                rows = this.state.rightsRows.map(oneRow => {
                    oneRow.selected = isSelected;
                    return oneRow;
                });
            } else {
                rows = [...this.state.rightsRows];
                rows[rowId].selected = isSelected;
            }
            this.setState({
                rightsRows: rows
            });
        };

        this.handleSearchPattern = searchPattern => {
            this.setState({ searchPattern });
        };

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

        this.handleBaseDnSelection = (treeViewItem) => {
            this.setState({
                usersSearchBaseDn: treeViewItem.dn,
            }, () => {
                if (!treeViewItem.children || treeViewItem.children.length === 0) {
                    this.onUsersDrawerCloseClick();
                }
            });
        }

        this.getEntryInfo = () => {
            const params = {
                serverId: this.props.editorLdapServer,
                baseDn: this.props.wizardEntryDn,
                scope: 'base',
                filter: '(|(objectClass=*)(objectClass=ldapSubEntry))',
                attributes: 'numSubordinates'
            };

            runGenericSearch(params, (resArray) => {
                const numSub = resArray.length === 1
                    ? resArray[0].split(':')[1]
                    // TODO: Assume a potential child entry in case of error or no value?
                    // That would allow to trigger the search for direct child entry.
                    : 1;
            });
        }

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReachedVisual: this.state.stepIdReachedVisual < id ? id : this.state.stepIdReachedVisual,
                savedStepId: id
            }, this.buildAciText() );

            if (id === 2) {
                if (this.state.targetAttrRows.length > 0) {
                    // Data already fetched.
                    return;
                }
                // Populate the table with the schema attribute names and OIDs.
                getAttributesNameAndOid(this.props.editorLdapServer, (resArray) => {
                    const targetAttrRows = resArray.map(item => {
                        return { cells: [item[0], item[1]], selected: false }
                    });
                    const attributeList = resArray.map(item => {
                        return item[0];
                    });
                    const tableModificationTime = Date.now();
                    this.setState({
                        attributeList,
                        targetAttrRows,
                        tableModificationTime
                    });
                });
            } else if (id === 9) {
                this.addAciToEntry();
            }
        };

        this.onBackVisual = ({ id }) => {
            this.setState({ savedStepId: id });
        };

        this.addAciToEntry = () => {
            const params = { serverId: this.props.editorLdapServer };
            let ldifArray = [];
            ldifArray.push(`dn: ${this.props.wizardEntryDn}`);
            ldifArray.push('changetype: modify');
            ldifArray.push('add: aci');
            ldifArray.push(`aci: ${this.state.aciTextNew}`);

            this.setState({
                modalSpinning: true
            });

            modifyLdapEntry(params, ldifArray, (result) => {
                this.props.refreshAciTable();
                this.setState({
                    commandOutput: result.errorCode === 0 ? 'Successfully added ACI!' : 'Failed to add ACI, error: ' + result.errorCode ,
                    resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                    adding: false
                }, () => { this.props.onReload() }); // refreshes tableView
                const opInfo = {  // This is what refreshes treeView
                    operationType: 'MODIFY',
                    resultCode: result.errorCode,
                    time: Date.now()
                }
                this.props.setWizardOperationInfo(opInfo);
            });

        };

        this.resetACIText = () => {
            const orig = this.state.aciText;
            this.setState({
                aciTextNew: orig
            });
        };

        this.handleChange = (e) => {
            const target = e.target;
            const value = target.type === 'checkbox' ? target.checked : target.value;
            const attr = target.id;

            this.setState({
                [attr]: value,
            });
        };

        this.handleTextChange = (value) => {
            this.setState({
                aciTextNew: value,
            });
        };
        // End constructor()
    }

    showTreeLoadingState = (isTreeLoading) => {
        this.setState({
            isTreeLoading,
            searching: isTreeLoading ? true : false
        });
    }

    closeAddBindRuleModal = () => {
        this.setState({
            showAddBindRuleModal: false
        });
    };

    openAddBindRule = () => {
        // Open modal
        this.setState({
            showAddBindRuleModal: true,
            bindRuleType: "userdn",
            bindRuleOperator: "=",
            bindRuleValue: "",
            authmethod: "none",
            authMethodOperator: "=",
            usersChosenOptions: [],
            usersAvailableOptions: [],
            dnsOperator: "=",
            ipOperator: "=",
            dns: "",
            ip: "",
        });
    };

    updateUserBindRulesState = () => {
        // This is just for checking the userdn. groupdn, and roledn rules
        // so we can control the "Add Rule" form menu options
        let haveUserRules = false;
        let haveUserAttrRules = false;
        let hasSelectionSelection = false;
        let specialSelect = this.state.specialSelection;
        for(const rule of this.state.bindRuleRows) {
            if (rule.cells[0].endsWith("dn")) {
                haveUserRules = true;
                if (rule.cells[0] === "userdn" &&
                    (rule.cells[2] === "ldap:///self" || rule.cells[2] === "ldap:///all" || rule.cells[2] === "ldap:///anyone" || rule.cells[2] === "ldap:///parent" )) {
                    // Okay we still have a "special entry"
                    hasSelectionSelection = true;
                }
            } else if (rule.cells[0] === "userattr") {
                haveUserAttrRules = true;
            }
        }
        this.setState({
            haveUserRules,
            haveUserAttrRules,
            specialSelection: hasSelectionSelection ? specialSelect : ""
        })
    };

    doAddBindRule = () => {
        let bindRow = {};
        let selection = this.state.specialSelection;

        if (this.state.bindRuleType === "User DN Aliases") {
            if (selection === "") {
                selection = "ldap:///anyone"; // default option
            }
            bindRow = {
                cells: ["userdn", "=", selection]
            };
        } else if (this.state.bindRuleType === "authmethod") {
            bindRow = {
                cells: ["authmethod", this.state.authMethodOperator, this.state.authmethod]
            };
        } else if (this.state.bindRuleType === "ssf") {
            bindRow = {
                cells: ["ssf", this.state.ssfOperator, this.state.ssf]
            };
        } else if (this.state.bindRuleType === "ip") {
            bindRow = {
                cells: ["ip", this.state.ipOperator, this.state.ip]
            };
        } else if (this.state.bindRuleType === "dns") {
            bindRow = {
                cells: ["dns", this.state.dnsOperator, this.state.dns]
            };
        } else if (this.state.bindRuleType === "userattr") {
            const userattrVal = this.getUserattrVal();
            bindRow = {
                cells: ["userattr", this.state.userattrOperator, userattrVal]
            };
        } else {
            let value = "";
            for (const entryIdx in this.state.usersChosenOptions) {
                if (entryIdx > 0) {
                    value += " || ";
                }
                const dn = this.state.usersChosenOptions[entryIdx].props.title;
                value += "ldap:///" + dn;
            }
            bindRow = {
                cells: [this.state.bindRuleType, this.state.bindRuleOperator, value]
            };
        }

        let rows = [...this.state.bindRuleRows];
        rows.push(bindRow);
        this.setState({
            bindRuleRows: rows,
            showAddBindRuleModal: false,
            specialSelection: selection
        }, () => { this.updateUserBindRulesState() });
    };

    removeBindRuleRow = (rowId) => {
        let revisedRows = [];
        for (const idx in this.state.bindRuleRows) {
            if (idx.toString() !== rowId.toString()) {
                revisedRows.push(this.state.bindRuleRows[idx]);
            }
        }

        this.setState({
            bindRuleRows: revisedRows,
        }, () => { this.updateUserBindRulesState() });
    }

    render () {
        const {
            aciText,
            aciTextNew,
            authMethods,
            bindRuleRows,
            bindRuleType,
            dns,
            ip,
            isAciSyntaxValid,
            isHostFilterValid,
            isOpenHostFilter,
            isSearchRunning,
            isUsersDrawerExpanded,
            ldapURL,
            newAciName,
            rightsRows,
            savedStepId,
            searchPattern,
            selectedHostType,
            sortBy,
            stepIdReachedVisual,
            tableModificationTime,
            target,
            target_from,
            target_to,
            targetAttrRows,
            targetFilter,
            userattrAttr,
            usersAvailableOptions,
            usersChosenOptions,
            usersSearchBaseDn,
        } = this.state;

        const aciNameComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Choose a name for this ACI, and set the Target</Text>
                </TextContent>
                <Grid className="ds-margin-top-xlg" hasGutter>
                    <GridItem span={1} className="ds-label">
                        Name
                    </GridItem>
                    <GridItem span={7}>
                        <TextInput
                            validated={newAciName === '' ? ValidatedOptions.error : ValidatedOptions.default }
                            id="newAciName"
                            value={newAciName}
                            type="text"
                            onChange={(str, e) => {this.handleChange(e)}}
                            aria-label="Text input ACI name"
                            autoComplete="off"
                        />
                    </GridItem>
                </Grid>

                <Grid className="ds-margin-top-xlg" hasGutter>
                    <GridItem span={1} className="ds-label">
                        Target
                    </GridItem>
                    <GridItem span={7}>
                        <TextInput
                            validated={target === '' ? ValidatedOptions.error : ValidatedOptions.default }
                            id="target"
                            value={target}
                            type="text"
                            onChange={(str, e) => {this.handleChange(e)}}
                            aria-label="Text input ACI target"
                            autoComplete="off"
                        />
                    </GridItem>
                </Grid>
            </>
        );

        const usersPanelContent = (
            <DrawerPanelContent isResizable>
                <DrawerHead>
                    <span tabIndex={isUsersDrawerExpanded ? 0 : -1} ref={this.usersDrawerRef}>
                        <strong>LDAP Tree</strong>
                    </span>
                    <DrawerActions>
                        <DrawerCloseButton onClick={this.onUsersDrawerCloseClick} />
                    </DrawerActions>
                </DrawerHead>

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
            </DrawerPanelContent>
        );

        const userDrawerContent = (
            <>
                <Divider />
                <div className="ds-margin-bottom-md" />
                <DualListSelector
                    availableOptions={usersAvailableOptions}
                    chosenOptions={usersChosenOptions}
                    availableOptionsTitle="Available Entries"
                    chosenOptionsTitle="Chosen Entries"
                    onListChange={this.usersOnListChange}
                    id="usersSelector"
                    className="ds-aci-dual-select"
                />
            </>
        );

        const usersComponent = (
            <>
                {isSearchRunning &&
                    <center className="ds-font-size-md"><Spinner size="sm"/>&nbsp;&nbsp;Searching database ...</center>
                }
                {!isSearchRunning &&
                    <SearchInput
                        placeholder="Search for entries ..."
                        value={searchPattern}
                        onChange={this.handleSearchPattern}
                        onClear={evt => this.handleSearchPattern('')}
                        onSearch={this.handleSearchClick}
                        className="ds-search-input"
                    />
                }
                <div className="ds-margin-bottom-md" />
                <TextContent>
                    <Text>
                        Search Base:
                        <Text
                            className="ds-left-margin"
                            component={TextVariants.a}
                            onClick={this.onUsersDrawerClick}
                            href="#"
                        >
                            {usersSearchBaseDn}
                        </Text>
                    </Text>
                </TextContent>

                <Drawer className="ds-margin-top" isExpanded={isUsersDrawerExpanded} onExpand={this.onDrawerExpand}>
                    <DrawerContent panelContent={usersPanelContent}>
                        <DrawerContentBody>{userDrawerContent}</DrawerContentBody>
                    </DrawerContent>
                </Drawer>
            </>
        );

        const bindRulesComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Define the Bind Rules <Tooltip
                        position="bottom"
                        content={
                            <div>
                                The bind rules in an ACI define the required bind parameters
                                that must meet so that Directory Server applies the ACI. For
                                example user-based access uses the "userdn" bind rule, while
                                group access uses "groupdn" bind rule.  There are other
                                combinations that are possible, as well as defining the
                                authentication method allowed or the connection security level.
                                Please see the Administration Guide for more information.
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>
                <AciBindRuleTable
                    key={this.state.bindRuleRows}
                    rows={this.state.bindRuleRows}
                    removeRow={this.removeBindRuleRow}
                />
                <Button
                    className="ds-margin-top-lg"
                    variant="primary"
                    onClick={this.openAddBindRule}
                    isSmall
                >
                    Add Bind Rule
                </Button>
            </>
        );

        const rightsComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Choose the Rights to Allow or Deny <Tooltip
                        position="bottom"
                        content={
                            <div>
                                This section defines what permissions and rights are
                                allowed/denied by this ACI.
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>
                <Select
                    variant={SelectVariant.single}
                    className="ds-margin-top-lg"
                    aria-label="Select rights"
                    onToggle={this.onToggleRights}
                    onSelect={this.onSelectRights}
                    selections={this.state.rightType}
                    isOpen={this.state.isOpenRights}
                >
                    <SelectOption key="allow" value="allow" />
                    <SelectOption key="deny" value="deny" />
                </Select>
                <Table
                    onSelect={this.rightsOnSelect}
                    aria-label="Selectable Table User Rights"
                    cells={this.rightsColumns}
                    rows={rightsRows}
                    variant="compact"
                    borders={false}
                    className="ds-margin-top-lg"
                >
                    <TableHeader />
                    <TableBody />
                </Table>
            </>
        );

        const targetAttrComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Choose the Target Attributes <Tooltip
                        position="bottom"
                        content={
                            <div>
                                You can control what entry attributes are affected by an ACI.
                                If no target attributes are defined then all attributes are
                                impacted by the ACI.  It is strongly discouraged to use the
                                comparison operator "!=" when defining target attributes
                                because it implicitly allows access all the other attributes
                                in the entry (including operational attributes).  This is a
                                potential unintended security risk. Instead, just use "=" to
                                only open up the attributes you want the ACI to allow.
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>

                <div className="ds-margin-top-xlg">
                    <div className="ds-inline ds-font-size-md">
                        <b>Comparison Operator</b>
                    </div>
                    <div className="ds-inline ds-left-margin ds-raise-field-md">
                        <Select
                            variant={SelectVariant.single}
                            aria-label="Select auth compare operator"
                            onToggle={this.onToggleTargetAttrOp}
                            onSelect={this.onSelectTargetAttrOp}
                            selections={this.state.targetAttrCompOp}
                            isOpen={this.state.isOpenTargetAttrOperator}
                        >
                            <SelectOption key="targetattrequals" value="=" />
                            <SelectOption key="targetattrequals" value="!=" />
                        </Select>
                    </div>
                </div>
                <GenericPagination
                    columns={this.targetsColumns}
                    rows={targetAttrRows}
                    actions={null}
                    isSelectable={true}
                    canSelectAll={false}
                    enableSorting={true}
                    tableModificationTime={tableModificationTime}
                    handleSelectedAttrs={this.handleSelectedAttrs}
                    isSearchable
                />
                { targetAttrRows.length === 0 &&
                    // <div className="ds-margin-bottom-md" />
                    <Bullseye className="ds-margin-top-lg">
                        <center><Spinner size="lg"/></center>
                    </Bullseye>
                }
            </>
        );

        const targetFilterComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Target Filter <Tooltip
                        position="bottom"
                        content={
                            <div>
                                Optionally set an LDAP search filter to further
                                refine which entries this ACI will be applied to.
                                Warning, using targetFilters can impact the server's
                                performance and should be used sparingly.
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>
                <Form className="ds-margin-top-xlg" autoComplete="off">
                    <Grid>
                        <GridItem span={2} className="ds-label">
                            Target Filter
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                id="targetFilter"
                                aria-label="Add target filter"
                                onChange={(str, e) => { this.handleChange(e) }}
                                value={targetFilter}
                                validated={targetFilter !== "" && !valid_filter(targetFilter) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={targetFilter === "" || valid_filter(targetFilter)}>
                                The filter must be enclosed with parentheses
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                </Form>
            </>
        );

        const moddnComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Moving Entries <Tooltip
                        position="bottom"
                        content={
                            <div>
                                If you need to control access for moving an entry
                                between specific subtrees then use the
                                target_from and target_to keywords. These
                                keywords use LDAP URL's to specify the database
                                resources/locations:
                                ldap:///uid=*,cn=staging,dc=example,dc=com,
                                ldap:///ou=people,dc=example,dc=com
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>
                <Form className="ds-margin-top-xlg" autoComplete="off">
                    <Grid>
                        <GridItem span={2} className="ds-label" title="Specifies the entries that can be moved.  Example: ldap:///uid=*,cn=staging,dc=example,dc=com">
                            Target From
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                id="target_from"
                                aria-label="Add target_from"
                                onChange={(str, e) => { this.handleChange(e) }}
                                value={target_from}
                                validated={target_from !== "" && !isValidLDAPUrl(target_from) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={target_from === "" || isValidLDAPUrl(target_from)}>
                                The LDAP URL must start with "ldap:///"
                            </FormHelperText>
                        </GridItem>
                        <GridItem span={2} className="ds-label" title="Specifies where the entries can be moved to.  Example: ldap:///ou=people,dc=example,dc=com">
                            Target To
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                id="target_to"
                                aria-label="Add target_to"
                                onChange={(str, e) => { this.handleChange(e) }}
                                value={target_to}
                                validated={target_to !== "" && !isValidLDAPUrl(target_to) ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={target_to === "" || isValidLDAPUrl(target_to)}>
                                The LDAP URL must start with "ldap:///"
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                </Form>
            </>
        );

        const timesComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>Define Day and Time Restrictions <Tooltip
                        position="bottom"
                        content={
                            <div>
                                These bind rules control access based off of specific days or times.
                                If no days are selected then all the days of the week are assumed.
                            </div>
                        }
                    >
                        <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                    </Tooltip></Text>
                </TextContent>
                <TextContent className="ds-margin-top-xlg">
                    <Text component={TextVariants.h4}>
                        Choose Days To Allow Access
                    </Text>
                </TextContent>
                <div className="ds-margin-top ds-margin-left">
                    <Checkbox
                        className=""
                        label="Sunday"
                        isChecked={this.state.sunday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="Sunday"
                        id="sunday"
                    />
                    <Checkbox
                        className=""
                        label="Monday"
                        isChecked={this.state.monday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="monday"
                        id="monday"
                    />
                    <Checkbox
                        className=""
                        label="Tuesday"
                        isChecked={this.state.tuesday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="Tuesday"
                        id="tuesday"
                    />
                    <Checkbox
                        className=""
                        label="Wednesday"
                        isChecked={this.state.wednesday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="wednesday"
                        id="wednesday"
                    />
                    <Checkbox
                        className=""
                        label="Thursday"
                        isChecked={this.state.thursday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="thursday"
                        id="thursday"
                    />
                    <Checkbox
                        className=""
                        label="Friday"
                        isChecked={this.state.friday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="friday"
                        id="friday"
                    />
                    <Checkbox
                        className=""
                        label="Saturday"
                        isChecked={this.state.saturday}
                        onChange={(checked, e) => this.handleChange(e)}
                        aria-label="saturday"
                        id="saturday"
                    />
                </div>

                <TextContent className="ds-margin-top-xlg">
                    <Text component={TextVariants.h4}>
                        Time Range Restrictions
                    </Text>
                </TextContent>
                <Grid className="ds-margin-top-lg ds-margin-left">
                    <GridItem span={2} className="ds-label" title="First part of the time range">
                        Time Start
                    </GridItem>
                    <GridItem span={2}>
                        <FormSelect
                            id="timeStartCompOp"
                            value={this.state.timeStartCompOp}
                            onChange={(str, e) => { this.handleChange(e)}}
                            aria-label="FormSelect Input"
                        >
                            <FormSelectOption key="=" label="=" value="=" />
                            <FormSelectOption key="!=" label="!=" value="!=" />
                            <FormSelectOption key=">" label=">" value=">" />
                            <FormSelectOption key="<" label="<" value="<" />
                            <FormSelectOption key=">=" label=">=" value=">=" />
                            <FormSelectOption key="<=" label="<=" value="<=" />
                        </FormSelect>
                    </GridItem>
                    <GridItem span={8}>
                        <TimePicker
                            className="ds-left-margin"
                            time={this.state.timeOfDayStart}
                            is24Hour
                            onChange={(val) => this.handleTimeChange(val, "timeOfDayStart")}
                        />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top ds-margin-left">
                    <GridItem span={2} className="ds-label" title="Second part of the time range">
                        Time End
                    </GridItem>
                    <GridItem span={2}>
                        <FormSelect
                            id="timeEndCompOp"
                            value={this.state.timeEndCompOp}
                            onChange={(str, e) => { this.handleChange(e)}}
                            aria-label="FormSelect Input"
                        >
                            <FormSelectOption key="=" label="=" value="=" />
                            <FormSelectOption key="!=" label="!=" value="!=" />
                            <FormSelectOption key=">" label=">" value=">" />
                            <FormSelectOption key="<" label="<" value="<" />
                            <FormSelectOption key=">=" label=">=" value=">=" />
                            <FormSelectOption key="<=" label="<=" value="<=" />
                        </FormSelect>
                    </GridItem>
                    <GridItem span={8}>
                        <TimePicker
                            className="ds-left-margin"
                            time={this.state.timeOfDayEnd}
                            is24Hour
                            onChange={(val) => this.handleTimeChange(val, "timeOfDayEnd")}
                        />
                    </GridItem>
                </Grid>
            </>
        );

        const reviewComponent = (
            <>
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Review & Edit <Tooltip
                            position="bottom"
                            content={
                                <div>
                                    Review the new ACI, and make any edits as needed,
                                    but once you edit the ACI you can not go back
                                    to the previous wizard steps without undoing your
                                    changes.
                                </div>
                            }
                        >
                            <a className="ds-font-size-md"><InfoCircleIcon className="ds-info-icon" /></a>
                        </Tooltip>
                    </Text>
                </TextContent>
                <Card className="ds-margin-top-lg">
                    <CardBody className="ds-indent">
                        <TextArea
                            className="ds-textarea"
                            id="aciTextNew"
                            value={aciTextNew}
                            onChange={this.handleTextChange}
                            aria-label="aci text edit area"
                            autoResize
                            resizeOrientation="vertical"
                        />
                        <Button
                            className="ds-margin-top"
                            key="undo"
                            variant="primary"
                            onClick={this.resetACIText}
                            isDisabled={aciText === aciTextNew}
                            isSmall
                        >
                            Undo Changes
                        </Button>
                    </CardBody>
                </Card>
            </>
        );

        const resultComponent = (
            <>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant={this.state.resultVariant}
                        isInline
                        title="Result for ACI addition"
                    >
                        {this.state.resultVariant === "success" ? "Successfully added ACI" : this.state.commandOutput}
                        {this.state.adding &&
                            <div>
                                <Spinner className="ds-left-margin" size="md" />
                                &nbsp;&nbsp;Adding ACI ...
                            </div>
                        }
                    </Alert>
                </div>
                {this.state.resultVariant === 'danger' &&
                    <Card isSelectable>
                        <CardTitle>ACI Value</CardTitle>
                        <CardBody>
                            {aciTextNew}
                        </CardBody>
                    </Card>
                }
            </>
        );

        const newAciStepsVisual = [
            {
                id: 1,
                name: "ACI Name & Target",
                component: aciNameComponent,
                canJumpTo: stepIdReachedVisual >= 1 && aciText === aciTextNew,
                enableNext: newAciName !== "" && target !== "" ,
            },
            {
                id: 2,
                name: 'Target Attributes',
                component: targetAttrComponent,
                canJumpTo: stepIdReachedVisual >= 2 && aciText === aciTextNew,
            },
            {
                id: 3,
                name: 'Target Filter',
                component: targetFilterComponent,
                canJumpTo: stepIdReachedVisual >= 3 && aciText === aciTextNew,
                enableNext: (targetFilter === "" || (targetFilter !== "" && valid_filter(targetFilter))),
            },
            {
                id: 4,
                name: 'Moving Entries',
                component: moddnComponent,
                canJumpTo: stepIdReachedVisual >= 4 && aciText === aciTextNew,
                enableNext: (target_from === "" || (target_from === "" || (target_from !== "" && isValidLDAPUrl(target_from)))) &&
                    (target_to === "" || (target_to === "" || (target_to !== "" && isValidLDAPUrl(target_to)))) &&
                    ((target_to === "" && target_from === "") || (target_to !== "" && target_from !== "")),
            },
            {
                id: 5,
                name: 'Access Rights',
                component: rightsComponent,
                canJumpTo: stepIdReachedVisual >= 5 && aciText === aciTextNew,
                enableNext: rightsRows.filter(item => item.selected)
                    .map(cells => cells.cells[0]).toString() !== ""
            },
            {
                id: 6,
                name: 'Bind Rules',
                component: bindRulesComponent,
                canJumpTo: stepIdReachedVisual >= 6 && aciText === aciTextNew,
                enableNext: bindRuleRows.length > 0,
            },
            {
                id: 7,
                name: 'Days & Times',
                component: timesComponent,
                canJumpTo: stepIdReachedVisual >= 7 && aciText === aciTextNew,
            },
            {
                id: 8,
                name: 'Review & Edit',
                component: reviewComponent,
                nextButtonText: 'Add ACI',
                canJumpTo: stepIdReachedVisual >= 8,
                hideBackButton: this.state.aciText !== this.state.aciTextNew,
            },
            {
                id: 9,
                name: 'Result',
                component: resultComponent,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReachedVisual >= 9,
                hideBackButton: true,
                enableNext: !this.state.adding,
            }];


        let title = <>
            ACI: &nbsp;&nbsp;<strong>{this.state.aciText}</strong>
        </>;

        return (
            <>
                <Wizard
                    isOpen={this.props.isWizardOpen}
                    onClose={this.props.toggleOpenWizard}
                    onNext={this.onNext}
                    onBack={this.onBackVisual}
                    onGoToStep={this.buildAciText}
                    startAtStep={savedStepId}
                    title="Add a new Access Control Instruction"
                    description={this.state.savedStepId < 2 ? "" : title}
                    steps={newAciStepsVisual}
                />
                <Modal
                    variant={ModalVariant.medium}
                    title="Add Bind Rule"
                    isOpen={this.state.showAddBindRuleModal}
                    onClose={this.closeAddBindRuleModal}
                    actions={[
                        <Button
                            key="add"
                            variant="primary"
                            onClick={this.doAddBindRule}
                            isDisabled={
                                (bindRuleType.endsWith("dn") && usersChosenOptions.length === 0) ||
                                (bindRuleType === "dns" && !isValidHostname(dns)) ||
                                (bindRuleType === "ip" && !isValidIpAddress(ip)) ||
                                (bindRuleType === "userattr" && userattrAttr === "")
                            }
                        >
                            Add Bind Rule
                        </Button>,
                        <Button
                            key="cancel"
                            variant="link"
                            onClick={this.closeAddBindRuleModal}
                        >
                            Cancel
                        </Button>
                    ]}
                >
                    <Grid className="ds-margin-top">
                        <GridItem span={3} className="ds-label">
                            Choose Bind Rule
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect id="bindRuleType" value={bindRuleType} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                { this.state.specialSelection === "" && !this.state.haveUserAttrRules &&
                                    <>
                                        <FormSelectOption key="userdn" label="User DN (userdn)" value="userdn" title="Bind rules for user entries"/>
                                        <FormSelectOption key="groupdn" label="Group DN (groupdn)" value="groupdn" title="Bind rules for groups" />
                                        <FormSelectOption key="roledn" label="Role DN (roledn)" value="roledn" title="Bind rules for Roles" />
                                    </>
                                }
                                {!this.state.haveUserRules && !this.state.haveUserAttrRules &&
                                    <FormSelectOption key="special" label="User DN Aliases (userdn)" value="User DN Aliases" title="Special bind rules for user DN catagories" />
                                }
                                {!this.state.haveUserRules && !this.state.haveUserAttrRules &&
                                    <FormSelectOption key="userattr" label="User Attribute (userattr)" value="userattr" title="Bind rule to specify which attribute must match between the entry used to bind to the directory and the targeted entry"/>
                                }
                                <FormSelectOption key="authmethod" label="Authentication Method (authmethod)" value="authmethod" title="Specify the authentication methods to restrict" />
                                {this.state.ssf === "" &&
                                    <FormSelectOption key="ssf" label="Connection Security (ssf)" value="ssf" title="Specify the connection security strength factor (ssf)" />
                                }
                                <FormSelectOption key="ip" label="IP Address (ip)" value="ip" title="Specify an IP address or range to restrict" />
                                <FormSelectOption key="hostname" label="Hostname (dns)" value="dns" title="Specify a hostname or domain to restrict" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    {bindRuleType === "User DN Aliases" &&
                        <Grid className="ds-margin-top">
                            <GridItem span={3} className="ds-label">
                                Select Alias URL
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect id="specialSelection" value={this.state.specialSelection} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                    <FormSelectOption key="anyone" label="ldap:///anyone" value="ldap:///anyone" title="Grants Anonymous Access"/>
                                    <FormSelectOption key="all" label="ldap:///all" value="ldap:///all" title="Grants Access to Authenticated Users" />
                                    <FormSelectOption key="self" label="ldap:///self" value="ldap:///self" title="Enables Users to Access Their Own Entries" />
                                    <FormSelectOption key="parent" label="ldap:///parent" value="ldap:///parent" title="Grants Access for Child Entries of a User" />
                                </FormSelect>

                            </GridItem>
                        </Grid>
                    }
                    {bindRuleType.endsWith("dn") &&
                        <>
                            <Grid className="ds-margin-top">
                                <GridItem span={3} className="ds-label">
                                    Choose Comparator
                                </GridItem>
                                <GridItem span={9}>
                                    <FormSelect id="bindRuleOperator" value={bindRuleType} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                        <FormSelectOption key="=" label="=" value="=" />
                                        <FormSelectOption key="!=" label="!=" value="!=" />
                                    </FormSelect>
                                </GridItem>
                            </Grid>
                            <Card isSelectable className="ds-margin-top">
                                <CardBody>
                                    {usersComponent}
                                </CardBody>
                            </Card>
                        </>
                    }
                    {bindRuleType === "ip" &&
                        <>
                            <Grid className="ds-margin-top">
                                <GridItem span={3} className="ds-label">
                                    IP Address
                                </GridItem>
                                <GridItem span={2}>
                                    <FormSelect id="ipOperator" value={this.state.ipOperator} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                        <FormSelectOption key="=" label="=" value="=" />
                                        <FormSelectOption key="!=" label="!=" value="!=" />
                                    </FormSelect>
                                </GridItem>
                                <GridItem span={7} className="ds-left-margin">
                                    <TextInput
                                        id="ip"
                                        aria-label="Add IP restriction"
                                        onChange={(str, e) => { this.handleChange(e) }}
                                        value={ip}
                                        autoComplete="off"
                                        validated={ip ==="" || (ip !== "" && !isValidIpAddress(ip)) ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                    {ip === "" || !isValidIpAddress(ip) &&
                                        <HelperText className="ds-left-margin">
                                            <HelperTextItem variant="error">Invalid format for IP address</HelperTextItem>
                                        </HelperText>
                                    }
                                </GridItem>
                            </Grid>

                        </>
                    }
                    {bindRuleType === "dns" &&
                        <>
                            <Grid className="ds-margin-top">
                                <GridItem span={3} className="ds-label">
                                    Hostname
                                </GridItem>
                                <GridItem span={2}>
                                    <FormSelect id="dnsOperator" value={this.state.dnsOperator} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                        <FormSelectOption key="=" label="=" value="=" />
                                        <FormSelectOption key="!=" label="!=" value="!=" />
                                    </FormSelect>
                                </GridItem>
                                <GridItem span={7} className="ds-left-margin">
                                    <TextInput
                                        id="dns"
                                        aria-label="Add Hostname restriction"
                                        onChange={(str, e) => { this.handleChange(e) }}
                                        value={dns}
                                        validated={dns ==="" || (dns !== "" && !isValidHostname(dns)) ? ValidatedOptions.error : ValidatedOptions.default}
                                        autoComplete="off"
                                    />
                                    {dns === "" || !isValidHostname(dns) &&
                                        <HelperText className="ds-left-margin">
                                            <HelperTextItem variant="error">Invalid format for hostname</HelperTextItem>
                                        </HelperText>
                                    }
                                </GridItem>
                            </Grid>

                        </>
                    }
                    {bindRuleType === "authmethod" &&
                        <Grid className="ds-margin-top">
                            <GridItem span={3} className="ds-label">
                                Authentication Method
                            </GridItem>
                            <GridItem span={2}>
                                <FormSelect
                                    id="authMethodOperator"
                                    value={this.state.authMethodOperator}
                                    onChange={(str, e) => { this.handleChange(e)}}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="=" label="=" value="=" />
                                    <FormSelectOption key="!=" label="!=" value="!=" />
                                </FormSelect>
                            </GridItem>
                            <GridItem span={7} className="ds-left-margin">
                                <FormSelect id="authmethod" value={this.state.authmethod} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                    <FormSelectOption key="none" value="none" label="none" />
                                    <FormSelectOption key="simple" value="simple" label="simple" />
                                    <FormSelectOption key="SSL" value="SSL" label="SSL" />
                                    <FormSelectOption key="SASL" value="SASL" label="SASL" />
                                    <FormSelectOption key="LDAPI" value="LDAPI" label="LDAPI" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                    }
                    {bindRuleType === "ssf" &&
                        <Grid className="ds-margin-top">
                            <GridItem span={3} className="ds-label" title="Security Strength Factor (ssf) - encryption key strength">
                                Connection Security
                            </GridItem>
                            <GridItem span={2}>
                                <FormSelect
                                    id="ssfOperator"
                                    value={this.state.ssfOperator}
                                    onChange={(str, e) => { this.handleChange(e)}}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="=" label="=" value="=" />
                                    <FormSelectOption key="!=" label="!=" value="!=" />
                                    <FormSelectOption key=">" label=">" value=">" />
                                    <FormSelectOption key="<" label="<" value="<" />
                                    <FormSelectOption key=">=" label=">=" value=">=" />
                                    <FormSelectOption key="<=" label="<=" value="<=" />
                                </FormSelect>
                            </GridItem>
                            <GridItem span={7} className="ds-left-margin">
                                <FormSelect
                                    id="ssf"
                                    value={this.state.ssf}
                                    onChange={(str, e) => { this.handleChange(e)}}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="0" label="0" value="0" title="No connection security restrictions" />
                                    <FormSelectOption key="128" label="128" value="128" />
                                    <FormSelectOption key="192" label="192" value="192" />
                                    <FormSelectOption key="256" label="256" value="256" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                    }
                    {bindRuleType === "userattr" &&
                        <Grid className="ds-margin-top">
                            <GridItem span={3} className="ds-label">
                                User Attribute
                            </GridItem>
                            <GridItem span={2}>
                                <FormSelect
                                    id="userattrOperator"
                                    value={this.state.userattrOperator}
                                    onChange={(str, e) => { this.handleChange(e)}}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="=" label="=" value="=" />
                                    <FormSelectOption key="!=" label="!=" value="!=" />
                                </FormSelect>
                            </GridItem>
                            <GridItem span={5} className="ds-left-margin">
                                <FormSelect id="userattrAttr" value={this.state.userattrAttr} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                    <FormSelectOption value="" label="Select an attribute" isPlaceholder />
                                    {this.state.attributeList.map((attr, index) => (
                                        <FormSelectOption key={attr} value={attr} label={attr} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                            <GridItem span={2} className="ds-left-margin">
                                <FormSelect id="userattrBindType" value={this.state.userattrBindType} onChange={(str, e) => { this.handleChange(e)}} aria-label="FormSelect Input">
                                    <FormSelectOption key="USERDN" value="USERDN" label="USERDN" />
                                    <FormSelectOption key="GROUPDN" value="GROUPDN" label="GROUPDN" />
                                    <FormSelectOption key="ROLEDN" value="ROLEDN" label="ROLEDN" />
                                    <FormSelectOption key="SELFDN" value="SELFDN" label="SELFDN" />
                                    <FormSelectOption key="LDAPURL" value="LDAPURL" label="LDAPURL" />
                                </FormSelect>
                            </GridItem>

                            <GridItem span={12} className="ds-margin-top ds-label" title="Extend the application of the ACI several levels below the targeted entry. This is possible by specifying the number of levels below the target that should inherit the ACI.">
                                Inheritance Levels
                            </GridItem>
                            <GridItem span={12} className="ds-left-indent ds-margin-top">
                                <Checkbox
                                    className=""
                                    label="0"
                                    isChecked={this.state.userAttrParent0}
                                    onChange={(checked, e) => this.handleChange(e)}
                                    aria-label="0"
                                    id="userAttrParent0"
                                    title="Default value, this userattr bind rule only applies to the target entry (no child entries)."
                                />
                                <Checkbox
                                    className=""
                                    label="1"
                                    isChecked={this.state.userAttrParent1}
                                    onChange={(checked, e) => this.handleChange(e)}
                                    aria-label="1"
                                    id="userAttrParent1"
                                />
                                <Checkbox
                                    className=""
                                    label="2"
                                    isChecked={this.state.userAttrParent2}
                                    onChange={(checked, e) => this.handleChange(e)}
                                    aria-label="2"
                                    id="userAttrParent2"
                                />
                                <Checkbox
                                    className=""
                                    label="3"
                                    isChecked={this.state.userAttrParent3}
                                    onChange={(checked, e) => this.handleChange(e)}
                                    aria-label="3"
                                    id="userAttrParent3"
                                />
                                <Checkbox
                                    className=""
                                    label="4"
                                    isChecked={this.state.userAttrParent4}
                                    onChange={(checked, e) => this.handleChange(e)}
                                    aria-label="4"
                                    id="userAttrParent4"
                                />
                            </GridItem>
                        </Grid>
                    }
                </Modal>
            </>
        );
    }
}

export default AddNewAci;
