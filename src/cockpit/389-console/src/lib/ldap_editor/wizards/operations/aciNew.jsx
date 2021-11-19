import React from 'react';
import {
  Alert,
  Wizard, DualListSelector, Button, Bullseye,
  Card, CardTitle, CardBody,
  Divider, ExpandableSection,
  Grid, GridItem, InputGroup, InputGroupText,
  TextInput, Label, Select, SelectOption, SelectVariant, Tooltip,
  Title,
  Spinner,
  Drawer, DrawerPanelContent, DrawerContent,
  DrawerContentBody, DrawerPanelBody, DrawerHead,
  DrawerActions, DrawerCloseButton,
  ValidatedOptions, TimePicker
} from '@patternfly/react-core';

import {
  Table,
  TableHeader,
  TableBody,
  headerCol,
  sortable,
  SortByDirection
} from '@patternfly/react-table';

import PlusCircleIcon from '@patternfly/react-icons/dist/js/icons/plus-circle-icon';
import SearchIcon from '@patternfly/react-icons/dist/js/icons/search-icon';
import InfoCircleIcon from '@patternfly/react-icons/dist/js/icons/info-circle-icon';
import ArrowRightIcon from '@patternfly/react-icons/dist/js/icons/arrow-right-icon';
import TimesIcon from '@patternfly/react-icons/dist/js/icons/times-icon';

import AciManualEdition from './aciManualEdit.jsx';
import GenericPagination from '../../lib/genericPagination.jsx';
import LdapNavigator from '../../lib/ldapNavigator.jsx';

import {
  runGenericSearch, decodeLine, getAttributesNameAndOid,
  isValidIpAddress
} from '../../lib/utils.jsx';

class AddNewAci extends React.Component {
  constructor (props) {
    super(props);

    this.specialRights = 'Special Rights';
    this.specialUsers = [
      <span title="Grants Access to Authenticated Users">All Authenticated Users</span>,
      <span title="Grants Anonymous Access">All Users</span>,
      <span title="Enables Users to Access Their Own Entries">Self</span>
    ];

    this.rightsColumns = [{ title: 'Name' }, { title: 'Description' }];
    this.targetsColumns = [
      { title: 'Name', transforms: [sortable] },
      { title: 'OID', transforms: [sortable] }
    ];
    this.hostsColumns = ['Host', 'Filter Type'];
    this.timesColumns = ['Day of Week', 'Start Time', 'End Time'];
    this.entryTypeOptions = ['Users', 'Groups', 'Roles', this.specialRights];
    this.hostFilterOptions = ['DNS Host Filter', 'IP Address Host Filter'];
    this.timesProps = {
      is24Hour: true,
      stepMinutes: 60,
      isDisabled: true,
      onChange: this.onChange
    }

    this.state = {
      // General
      editVisually: true,
      stepIdReachedVisual: 1,
      savedStepId: 1,
      isAciSyntaxValid: false,
      isModalOpen: false,
      isOpenEntryType: false,
      selectedEntryType: null,
      searchPattern: '',
      newAciName: '',
      isTreeLoading: false,
      searching: false,
      // Users
      usersSearchBaseDn: this.props.wizardEntryDn,
      isSearchRunning: false,
      usersAvailableOptions: [],
      usersChosenOptions: [
        <span title="Grants Anonymous Access">
          All Users
        </span>
      ],
      isUsersDrawerExpanded: false,
      isTargetsDrawerExpanded: false,
      // Rights
      rightsRows: [
        { cells: ['read', 'See the values of targeted attributes'], selected: true },
        { cells: ['compare', 'Compare targeted attribute values'], selected: true },
        { cells: ['search', 'Determine if targeted attributes exist'], selected: true },
        { cells: ['selfwrite', 'Add one\'s own DN to the target'], selected: true },
        { cells: ['write', 'Modify targeted attributes'], selected: true },
        { cells: ['delete', 'Remove targeted entries'], selected: true },
        { cells: ['add', 'Add targeted entries'], selected: true },
        { cells: ['proxy', 'Authenticate as another user'] }
      ],
      // Targets
      targetsRows: [],
      sortBy: {},
      tableModificationTime: 0,
      // Hosts
      selectedHostType: null,
      isOpenHostFilter: false,
      hostFilterValue: '',
      isHostFilterValid: false,
      hostsRows: [],
      hostFilterFound: false,
      // Times
      timesRows: [
        ['Sunday', // TOFIX: DONE FOR DEMO.
          { title: <TimePicker is24Hour stepMinutes={60} onChange={this.onChange} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }],
        ['Monday',
          { title: <TimePicker {...this.timesProps} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }],
        ['Tuesday',
          { title: <TimePicker {...this.timesProps} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }],
        ['Wednesday',
          { title: <TimePicker {...this.timesProps} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }],
        ['Thursday',
          { title: <TimePicker {...this.timesProps} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }],
        ['Friday',
          { title: <TimePicker {...this.timesProps} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }],
        ['Saturday',
          { title: <TimePicker {...this.timesProps} defaultTime="00:00" /> },
          { title: <TimePicker {...this.timesProps} defaultTime="23:00" /> }]]
    };

    // TODO: Define a generic Drawer component that shows the LDAP tree
    // rather than defining everything twice!!!
    this.usersDrawerRef = React.createRef();
    this.targetsDrawerRef = React.createRef();

    // Users
    this.onToggleEntryType = isOpenEntryType => {
      this.setState({
        isOpenEntryType
      });
    };
    this.onSelectEntryType = (event, selection) => {
      this.setState({
        selectedEntryType: selection,
        isOpenEntryType: false,
        usersAvailableOptions: selection === this.specialRights
          ? this.specialUsers
          : []
      });
    };
    // Hosts
    this.onToggleHostFilter = isOpenHostFilter => {
      this.setState({
        isOpenHostFilter
      });
    };
    this.onSelectHostFilter = (event, selection) => {
      this.setState({
        selectedHostType: selection,
        isOpenHostFilter: false
      });
    };
    this.handleHostFilter = hostFilterValue => {
      const isHostFilterValid = isValidIpAddress(hostFilterValue);
      this.setState({
        hostFilterValue,
        isHostFilterValid
      });
    };
    //
    this.handleModalToggle = () => {
      this.setState(({ isModalOpen }) => ({
        isModalOpen: !isModalOpen
      }));
    };

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
      this.setState({
        isSearchRunning: true,
        usersAvailableOptions: []
      }, this.getEntries);
    };

    this.getEntries = () => {
      const searchArea = this.state.selectedEntryType;
      const baseDn = this.state.usersSearchBaseDn;

      let filter = '';
      let attrs = '';
      const pattern = this.state.searchPattern;
      if (searchArea === 'Users') {
        filter = pattern === ''
          ? '(|(objectClass=person)(objectClass=nsPerson))'
          : `(&(|(objectClass=person)(objectClass=nsPerson)(objectClass=nsAccount)(objectClass=nsOrgPerson)(objectClass=posixAccount))(|(cn=*${pattern}*)(uid=${pattern})))`;
        attrs = 'cn uid'
      } else if (searchArea === 'Groups') {
        filter = pattern === ''
          ? '(|(objectClass=groupofuniquenames)(objectClass=groupofnames))'
          : `(&(|(objectClass=groupofuniquenames)(objectClass=groupofnames))(cn=*${pattern}*))`;
        attrs = 'cn';
      } else if (searchArea === 'Roles') {
        filter = pattern === ''
          ? '(&(objectClass=ldapsubentry)(objectClass=nsRoleDefinition))'
          : `(&(objectClass=ldapsubentry)(objectClass=nsRoleDefinition)(cn=*${pattern}*))`;
        attrs = 'cn';
      }

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

      // USERS
      // ... SRCH base="dc=gsslab,dc=brq,dc=redhat,dc=com" scope=2 filter="(objectClass=person)" attrs=ALL
      // Pattern = "Tek"
      // ... SRCH base="dc=gsslab,dc=brq,dc=redhat,dc=com" scope=2 filter="(&(objectClass=person)(|(cn=*Tek*)(uid=Tek)))" attrs=ALL
      // GROUPS
      // ... SRCH base="dc=gsslab,dc=brq,dc=redhat,dc=com" scope=2 filter="(objectClass=groupofuniquenames)" attrs=ALL
      // Pattern = "Tek"
      // ... SRCH base="dc=gsslab,dc=brq,dc=redhat,dc=com" scope=2 filter="(&(objectClass=groupofuniquenames)(cn=*Tek*))" attrs=ALL
      // ROLES
      // ... SRCH base="dc=gsslab,dc=brq,dc=redhat,dc=com" scope=2 filter="(&(objectClass=ldapsubentry)(objectClass=nsRoleDefinition))" attrs=ALL
      // Pattern = "Teko"
      // ... SRCH base="dc=gsslab,dc=brq,dc=redhat,dc=com" scope=2 filter="(&(objectClass=ldapsubentry)(objectClass=nsRoleDefinition)(cn=*Teko*))" attrs=ALL
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

    // Targets:
    this.handleHostFilterClick = () => {
      const rows = this.state.hostsRows;
      const filter = this.state.hostFilterValue;
      const type = this.state.selectedHostType.startsWith('DNS')
        ? 'DNS Name'
        : 'IP Address';
      const found = rows.find(item =>
        (item.cells[0] === filter) && (item.cells[1] === type)
      );
      if (found) {
        this.setState({ hostFilterFound: true });
      } else {
        rows.push({ cells: [filter, type] });
        this.setState({
          hostsRows: rows,
          hostFilterFound: false
        });
      }
    };
    // Times:
    this.timesOnSelect = (event, isSelected, rowId) => {
      let rows;
      if (rowId === -1) {
        rows = this.state.timesRows.map(oneRow => {
          oneRow.selected = isSelected;
          return oneRow;
        });
      } else {
        rows = [...this.state.timesRows];
        rows[rowId].selected = isSelected;
      }
      this.setState({
        timesRows: rows
      });
    };
    /* this.targetsOnSelect = (event, isSelected, rowId) => {
      let rows;
      if (rowId === -1) {
        rows = this.state.targetsRows.map(oneRow => {
          oneRow.selected = isSelected;
          return oneRow;
        });
      } else {
        rows = [...this.state.targetsRows];
        rows[rowId].selected = isSelected;
      }
      this.setState({
        targetsRows: rows
      });
    }; */

    this.handleAciName = newAciName => {
      this.setState({ newAciName });
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
      // TODO: There must be a shorter way to remove duplicates ;-)
      //
      // Remove duplicates in the chosen options if any:
      /* const alreadyChosen = [...this.state.usersChosenOptions];
      const alreadyChosenValues = alreadyChosen.map(item => item.props.title);
      const realNewOptions = newChosenOptions
        .filter((item) => {
          return !alreadyChosenValues.includes(item.props.title);
        });
      const finalChosenOptions = [...alreadyChosen, ...realNewOptions];
      finalChosenOptions.map(item => {
        console.log(item);
        console.log(`Option = ${item.props.title}`);
        // console.log(`Option = ${item[0].value}`)
      });   */

      const newAvailNoDups = this.removeDuplicates(newAvailableOptions);
      const newChosenNoDups = this.removeDuplicates(newChosenOptions);

      this.setState({
        usersAvailableOptions: newAvailNoDups.sort(),
        usersChosenOptions: newChosenNoDups.sort()
      });
    };

    this.handleBaseDnSelection = (treeViewItem) => {
      this.setState({
        usersSearchBaseDn: treeViewItem.dn
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
        // console.log(`getEntryInfo() => numSub=${numSub}`);
      });
    }

    /* this.onSort = (_event, index, direction) => {
      const sortedRows = this.state.targetsRows
        .sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
      this.setState({
        sortBy: {
          index,
          direction
        },
        targetsRows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
      });
    }; */

    this.handleManualOrVisual = () => {
      this.setState(({ editVisually }) => ({
        editVisually: !editVisually
      }));
    };

    this.onNextVisual = ({ id }) => {
      this.setState({
        stepIdReachedVisual: this.state.stepIdReachedVisual < id ? id : this.state.stepIdReachedVisual,
        savedStepId: id
      });

      if (id === 4) {
        // console.log(`this.state.targetsRows.length = ${this.state.targetsRows.length}`);
        if (this.state.targetsRows.length > 0) {
          // Data already fetched.
          // If schema has been changed, user needs to reload the page anyway
          // thus resetting the targetsRows to an empty array.
          return;
        }
        // Populate the table with the schema attribute names and OIDs.
        getAttributesNameAndOid(this.props.editorLdapServer, (resArray) => {
          const targetsRows = resArray.map(item => {
            return { cells: [item[0], item[1]], selected: true }
          });
          const tableModificationTime = Date.now();
          this.setState({
            targetsRows,
            tableModificationTime
          });
        });
      } else if (id === 6) {
        this.addAciToEntry();
      }
    };

    this.onBackVisual = ({ id }) => {
      this.setState({ savedStepId: id });
    };

    /* dn: dc=example,dc=com
changetype: modify
add: aci
aci: (targetattr = "*") (version 3.0;acl "Deny example.com"; deny (all)
 (userdn = "ldap:///anyone") and (dns != "*.example.com");) */

    this.addAciToEntry = () => {
      const allRows = this.state.targetsRows;
      const selectedTargets = allRows
        .filter(item => item.selected)
        .map(cells => cells[0]);
      let myTargets = '*';
      // TODO: Take unselected attributes if their total is lower than the selected ones
      // and use the NOT operator.
      if (selectedTargets.length !== allRows.length) {
        for (const attr of selectedTargets) {
          myTargets += attr + ' || '
        }
        // Remove the trailing ' || '
        myTargets = myTargets.slice(0, -4);
      }

      const myRights = this.state.rightsRows
        .filter(item => item.selected)
        .map(cells => cells[0])
        .toString();
      const name = this.state.newAciName;
      const newAci = '';
    };
    // End constructor().
  }

  showTreeLoadingState = (isTreeLoading) => {
      this.setState({
          isTreeLoading,
          searching: isTreeLoading ? true : false
      });
  }

  render () {
    const {
      editVisually, isAciSyntaxValid,
      newAciName, selectedEntryType, isOpenEntryType,
      usersAvailableOptions, usersChosenOptions, usersSearchBaseDn,
      isSearchRunning, searchPattern,
      stepIdReachedVisual, savedStepId,
      rightsRows, targetsRows,
      isUsersDrawerExpanded,
      sortBy, tableModificationTime,
      selectedHostType, isOpenHostFilter, isHostFilterValid, hostsRows,
      hostFilterValue, hostFilterFound,
      timesRows
    } = this.state;

    const aciNameComponent = (
      <>
        <Grid hasGutter>
          {/* <GridItem span={3}>
            <Tooltip
              position="top"
              content={
                <div>{newAciName === '' ? 'Choose a name for the ACI.' : 'ACI Name is filled.'}</div>
              }
            >
              <Label color={newAciName === '' ? 'red' : 'blue'} icon={<InfoCircleIcon />}>
              ACI Name
              </Label>
            </Tooltip>
            </GridItem>
          <GridItem span={6}>
            <TextInput
              value={newAciName}
              type="text"
              onChange={this.handleAciName}
              aria-label="Text input ACI name"
            /> */}

          <GridItem span={9}>
            <InputGroup>
              <InputGroupText id="aciname" aria-label="ACI Name">
                ACI Name
              </InputGroupText>
              <TextInput
                validated={newAciName === '' ? ValidatedOptions.error : ValidatedOptions.default }
                id="aciNameInput"
                value={newAciName}
                type="text"
                onChange={this.handleAciName}
                aria-label="Text input ACI name"
                isDisabled={!editVisually}
                autoComplete="off"
              />
            </InputGroup>

          </GridItem>
          <GridItem span={3}>
            <Button
              className="ds-addons-float-right"
              variant="tertiary"
              onClick={this.handleManualOrVisual}
            >
              {editVisually ? 'Edit Manually' : 'Edit Visually'}
            </Button>
          </GridItem>
        </Grid>

        <div className="ds-margin-bottom-md" />
        {/* <Divider inset={{ default: 'insetMd' }} /> */}
        <Divider />
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

      </DrawerPanelContent>
    );

    const userDrawerContent = (
      <>
        <Divider />
        <div className="ds-margin-bottom-md" />

        <DualListSelector
          availableOptions={usersAvailableOptions}
          chosenOptions={usersChosenOptions}
          availableOptionsTitle="Available Users"
          chosenOptionsTitle="Chosen Users"
          onListChange={this.usersOnListChange}
          id="usersSelector"
        />
      </>
    );

    const usersComponent = (
      <React.Fragment>
        {aciNameComponent}
        <Grid hasGutter className="ds-margin-top">
          <GridItem span={3}>
            <Select
              variant={SelectVariant.single}
              aria-label="Select Input for entry type"
              onToggle={this.onToggleEntryType}
              onSelect={this.onSelectEntryType}
              selections={selectedEntryType}
              isOpen={isOpenEntryType}
              placeholderText="Search Options"
            >
              {this.entryTypeOptions.map((option, index) => (
                <SelectOption
                  key={`entryType_${index}`}
                  value={option}
                  // isPlaceholder={false}
                />
              ))}
            </Select>
          </GridItem>
          <GridItem span={9}>
            <InputGroup>
              <TextInput
                id="searchPattern"
                aria-label="Text input to search the target DN"
                value={searchPattern}
                onChange={this.handleSearchPattern}
                isDisabled={(selectedEntryType === null) || (selectedEntryType === this.specialRights)}
                autoComplete="off"
              />
              <Button
                id="buttonSearchPattern"
                variant="control"
                isDisabled={isSearchRunning || (selectedEntryType === null) || (selectedEntryType === this.specialRights)}
                onClick={this.handleSearchClick}
                isLoading={isSearchRunning}
              >
                {!isSearchRunning && <SearchIcon />}{' '}Search
              </Button>
            </InputGroup>
          </GridItem>
        </Grid>

        <div className="ds-margin-bottom-md" />

        {(selectedEntryType !== null) && (selectedEntryType !== this.specialRights) &&
          <>
            <Label onClick={this.onUsersDrawerClick} href="#" variant="outline" color="blue" icon={<InfoCircleIcon />}>
              Search Base DN
            </Label>
            <strong>&nbsp;&nbsp;{usersSearchBaseDn}</strong>
          </>
        }

        <Drawer className="ds-margin-top" isExpanded={isUsersDrawerExpanded} onExpand={this.onDrawerExpand}>
          <DrawerContent panelContent={usersPanelContent}>
            <DrawerContentBody>{userDrawerContent}</DrawerContentBody>
          </DrawerContent>
        </Drawer>
      </React.Fragment>
    );

    const rightsComponent = (
      <>
        {aciNameComponent}
        <Table
          onSelect={this.rightsOnSelect}
          aria-label="Selectable Table User Rights"
          cells={this.rightsColumns}
          rows={rightsRows}
          variant="compact"
          borders={false}
          caption="Choose what users can do if they have access permission"
        >
          <TableHeader />
          <TableBody />
        </Table>
      </>
    );

    const targetsComponent = (
      <>
        {aciNameComponent}

        <ExpandableSection toggleText={`Target Directory Entry: "${this.props.wizardEntryDn}"`}
        >
          <Card isHoverable className="ds-indent ds-margin-bottom-md">
            <CardTitle>LDAP Tree</CardTitle>
            <CardBody>
              <LdapNavigator
                treeItems={[...this.props.treeViewRootSuffixes]}
                editorLdapServer={this.props.editorLdapServer}
                skipLeafEntries={true}
                handleNodeOnClick={this.handleBaseDnSelection}
                // TODO: Fix in the child compenent by checking if 'typeof ...' is a function.
                showTreeLoadingState={() => {}}
              />
            </CardBody>
          </Card>
        </ExpandableSection>
        <Divider />
        <div className="ds-margin-bottom-md" />

        <GenericPagination
          columns={this.targetsColumns}
          rows={targetsRows}
          actions={null}
          caption="These attributes are affected for all entries"
          isSelectable={true}
          canSelectAll={false}
          // onSelect={this.targetsOnSelect}
          enableSorting={true}
          // sortBy={sortBy}
          // onSort={this.onSort}
          tableModificationTime={tableModificationTime}
        />

        { targetsRows.length === 0 &&
        // <div className="ds-margin-bottom-md" />
        <Bullseye>
          <Title headingLevel="h2" size="lg">
          Loading...
          </Title>
          <center><Spinner size="xl"/></center>
        </Bullseye> }
      </>
    );

    const hostsComponent = (
      <>
        {aciNameComponent}
        <div className="ds-margin-bottom-md" />
        {hostFilterFound &&
        <>
          <Alert variant="danger" title="This host filter is already added." />
          <div className="ds-margin-bottom-md" />
        </>
        }
        <Grid hasGutter>
          <GridItem span={4}>
            <Select
              variant={SelectVariant.single}
              aria-label="Select Input to filter hosts"
              onToggle={this.onToggleHostFilter}
              onSelect={this.onSelectHostFilter}
              selections={selectedHostType}
              isOpen={isOpenHostFilter}
              placeholderText="Select Filter Type"
            >
              {this.hostFilterOptions.map((option, index) => (
                <SelectOption
                  key={`hostFilter_${index}`}
                  value={option}
                  isDisabled={index === 0}
                />
              ))}
            </Select>
          </GridItem>
          <GridItem span={8}>
            <InputGroup>
              <TextInput
                id="addFilter"
                aria-label="Add Host FIlter"
                onChange={this.handleHostFilter}
                isDisabled={selectedHostType === null}
                value={hostFilterValue}
                validated={hostFilterValue === ''
                  ? ValidatedOptions.default
                  : isHostFilterValid
                    ? ValidatedOptions.success
                    : ValidatedOptions.error }
              />
              <Button
                id="buttonHostFilter"
                variant="control"
                isDisabled={!isHostFilterValid}
                onClick={this.handleHostFilterClick}
              >
                <PlusCircleIcon/>{' '}Add Filter
              </Button>
            </InputGroup>
          </GridItem>
        </Grid>
        <div className="ds-margin-bottom-md" />
        <Table
          aria-label="Table Host Filter"
          cells={this.hostsColumns}
          rows={hostsRows}
          variant="compact"
          borders={false}
        >
          <TableHeader />
          <TableBody />
        </Table>
      </>
    );

    const timesComponent = (
      <>
        {aciNameComponent}
        <div className="ds-margin-bottom-md" />
        <Table
          onSelect={this.timesOnSelect}
          aria-label="Selectable Table Access Times"
          cells={this.timesColumns}
          rows={timesRows}
          variant="compact"
          caption="Access is allowed at the following times:"
        >
          <TableHeader />
          <TableBody />
        </Table>
      </>
    );

    const resultComponent = (
      // TODO ==> Use a CTA button to create the ACI and show the result here!
      // Better than adding an additional step ;-)
      <>
        {aciNameComponent}
        <Table
          onSelect={this.rightsOnSelect}
          aria-label="Selectable Table User Rights"
          cells={this.rightsColumns}
          rows={rightsRows}
          variant="compact"
          borders={false}
          caption="Choose what users can do if they have access permission."
        >
          <TableHeader />
          <TableBody />
        </Table>
      </>
    );

    const newAciStepsVisual = [
      {
        id: 1,
        name: this.props.firstStep[0].name,
        component: this.props.firstStep[0].component,
        canJumpTo: stepIdReachedVisual >= 1 && stepIdReachedVisual < 7,
        hideBackButton: true
      },
      {
        id: 2,
        name: 'Bind Rules',
        component: usersComponent,
        canJumpTo: stepIdReachedVisual >= 2,
        enableNext: !isUsersDrawerExpanded && usersChosenOptions.length > 0
      },
      {
        id: 3,
        name: 'Rights',
        component: rightsComponent,
        canJumpTo: stepIdReachedVisual >= 3
      },
      {
        id: 4,
        name: 'Targets',
        component: targetsComponent,
        canJumpTo: stepIdReachedVisual >= 4,
        enableNext: targetsRows.length > 0
      },
      {
        id: 5,
        name: 'Hosts',
        component: hostsComponent,
        canJumpTo: stepIdReachedVisual >= 5
      },
      {
        id: 6,
        name: 'Times',
        component: timesComponent,
        canJumpTo: stepIdReachedVisual >= 6,
        nextButtonText: 'Add ACI',
        enableNext: false // newAciName.length > 0
      },
      {
        id: 7,
        name: 'Review',
        component: resultComponent,
        nextButtonText: 'Finish',
        canJumpTo: stepIdReachedVisual >= 7
      }];

    if (editVisually) { // Visual mode.
      return (
        <Wizard
          isOpen={this.props.isWizardOpen}
          onClose={this.props.toggleOpenWizard}
          onNext={this.onNextVisual}
          onBack={this.onBackVisual}
          startAtStep={savedStepId}
          title="Add a new Access Control Instruction"
          description={`Entry DN: ${this.props.wizardEntryDn}`}
          steps={newAciStepsVisual}
        />
      );
    }

    return ( // Manual mode.
      <AciManualEdition
        firstStep={this.props.firstStep}
        isWizardOpen={this.props.isWizardOpen}
        toggleOpenWizard={this.props.toggleOpenWizard}
        aciNameComponent={aciNameComponent}
        updateAciName={this.handleAciName}
        wizardEntryDn={this.props.wizardEntryDn}
      />
    );
  }
}

export default AddNewAci;
