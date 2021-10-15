import React from 'react';

import {
  Button, Dropdown, DropdownToggle, DropdownGroup, DropdownItem,
  Select, SelectOption, SelectVariant,
  TextContent, TextList, TextListVariants,
  TextListItem, TextListItemVariants, Title,
  Popover, Tooltip
} from '@patternfly/react-core';

import {
  OnRunningIcon, UnpluggedIcon, ThIcon
} from '@patternfly/react-icons';

import EditorTreeView from './treeView.jsx';
import EditorTableView from './tableView.jsx';
import GenericWizard from './wizards/genericWizard.jsx'

import { setSizeLimit, setTimeLimit } from './lib/options.jsx';
import { ENTRY_MENU } from './lib/constants.jsx';

class EditorGenericView extends React.Component {
  constructor (props) {
    super(props);
    this.state = {
      isOpenMainOptions: false,
      showTreeView: true,
      sizeLimitIsEnabled: true,
      timeLimitIsEnabled: true,
      treeVisibleAndFirstClicked: false,
      hideSelectedEntry: false,
      entryMenuIsOpen: false,
      wizardName: '',
      isWizardOpen: false,
      wizardEntryDn: '',
      wizardOperationInfo: { operationType: '', resultCode: -1, time: 0 },
      refreshEntryTime: 0
    };

    this.onToggleMainOptions = isOpen => {
      this.setState({
        isOpenMainOptions: isOpen
      });
    };

    this.onSelectMainOptions = event => {
      const aTarget = event.target;

      switch (aTarget.name) {
        // Reload the whole page.
        case 'reload':
          this.props.updatePopoverAndTable();
          break;
        // Switch between tree view and table view.
        case 'viewType': {
          const showTreeView = aTarget.value === 'table';
          this.props.setPageSectionVariant(showTreeView); // Will use a light background for the tree.
          this.setState({
            showTreeView
          });
          break; }
        // Hide the selected entry details.
        case 'hideEntry':
          this.setState({ hideSelectedEntry: !this.state.hideSelectedEntry });
          break;
        case 'special':
          this.props.loadSpecialSuffixes();
          break;
        // Size and time limits.
        case 'sizeLimit': {
          this.setState({ sizeLimitIsEnabled: !this.state.sizeLimitIsEnabled },
            () => {
              const newSizeLimit = this.state.sizeLimitIsEnabled
                ? 1000 // TODO: Make the limit configurable ( 1000, 2000, 5000 seconds ?)
                : 0;
              setSizeLimit(newSizeLimit);
            });
          break; }
        case 'timeLimit': {
          this.setState({ timeLimitIsEnabled: !this.state.timeLimitIsEnabled },
            () => {
              const newTimeLimit = this.state.timeLimitIsEnabled
                ? 5 // TODO: Make the limit configurable ( 5, 10, 30 seconds ?)
                : 0;
              setTimeLimit(newTimeLimit);
            });
          break; }
        default:
          console.log(`Unknown command in onSelectMainOptions(): ${aTarget.name}`);
      }

      // Close the dropdown.
      this.setState({
        isOpenMainOptions: !this.state.isOpenMainOptions
      });
      this.onFocusMainOptions();
    };

    this.onFocusMainOptions = () => {
      const element = document.getElementById('editor-options-dropdown');
      element.focus();
    };

    // Show entry details table when user first clicks on the tree:
    this.setTreeFirstClicked = (value) => {
      this.setState({ treeVisibleAndFirstClicked: value });
    };

    // Set the operation type and the result code
    this.setWizardOperationInfo = (opInfo) => {
      // typeAndResult is an object with two fields (opType and result)
      // eg: {operationType: 'MODRDN', resultCode: 0, time: 1613090160492}
      const wizardOperationInfo = { ...opInfo };
      this.setState({ wizardOperationInfo })
    }

    // Actions when user clicks on the Entry Details menu
    this.onToggleEntryMenu = isOpen => {
      this.setState({
        entryMenuIsOpen: isOpen
      });
    };

    this.onSelectEntryOptions = event => {
      const aTarget = event.target;

      // No need to use a wizard for the Refresh operation.
      if (aTarget.name === ENTRY_MENU.refresh) {
        this.setState({
          entryMenuIsOpen: !this.state.entryMenuIsOpen,
          refreshEntryTime: Date.now()
        })
        return;
      }

      this.setState({
        entryMenuIsOpen: !this.state.entryMenuIsOpen,
        wizardName: aTarget.name,
        isWizardOpen: !this.state.isWizardOpen,
        wizardEntryDn: aTarget.value
      });
    }

    this.toggleOpenWizard = () => {
      this.setState({
        isWizardOpen: !this.state.isWizardOpen
      });
    };
  } // End constructor.

  render () {
    const {
      monitor,
      serverReachable, loading, currentDn
    } = this.props;

    const {
      isOpenMainOptions, showTreeView, treeVisibleAndFirstClicked,
      sizeLimitIsEnabled, timeLimitIsEnabled,
      hideSelectedEntry, entryMenuIsOpen,
      isWizardOpen, wizardEntryDn, wizardName, wizardOperationInfo,
      refreshEntryTime
    } = this.state;

    const dropdownItems = [
      <DropdownGroup label="Resources" key="options-limits">
        <DropdownItem key="options-limits-size"
          component="button" name="sizeLimit"
          tooltip={sizeLimitIsEnabled ? 'Current value is 1000' : null}
        >
          {sizeLimitIsEnabled ? 'Disable Size Limit' : 'Enable Size Limit (1000)'}
        </DropdownItem>
        <DropdownItem key="options-limits-time"
          component="button" name="timeLimit"
          value={timeLimitIsEnabled}
          tooltip={timeLimitIsEnabled ? 'Current value is 5 seconds' : null}
        >
          {timeLimitIsEnabled ? 'Disable Time Limit' : 'Enable Time Limit (5 s)'}
        </DropdownItem>
      </DropdownGroup>
    ];

    const treeItemsProps = wizardName === 'acis'
        ? { treeViewRootSuffixes: this.props.treeViewRootSuffixes }
        : {};

    return (
      <React.Fragment>
        {isWizardOpen &&
          <GenericWizard
            wizardName={wizardName}
            isWizardOpen={isWizardOpen}
            toggleOpenWizard={this.toggleOpenWizard}
            wizardEntryDn={wizardEntryDn}
            editorLdapServer={this.props.serverId}
            {...treeItemsProps}
            setWizardOperationInfo={this.setWizardOperationInfo}
          />
        }

        <div className="ds-addons-float-right same-line">
          <Dropdown
            onSelect={this.onSelectMainOptions}
            toggle={
              <DropdownToggle toggleIndicator={null} onToggle={this.onToggleMainOptions}
                aria-label="Options" id="editor-options-dropdown">
                <ThIcon />
              </DropdownToggle>
            }
            isOpen={isOpenMainOptions}
            dropdownItems={dropdownItems}
            isGrouped
            isPlain
            position="right"
          />
        </div>

        { showTreeView &&
            <EditorTreeView
              key="ldap-editor-treeview"
              onToggleEntryMenu={this.onToggleEntryMenu}
              onSelectEntryOptions={this.onSelectEntryOptions}
              entryMenuIsOpen={entryMenuIsOpen}
              treeViewRootSuffixes={this.props.treeViewRootSuffixes}
              loading={loading}
              serverReachable={serverReachable}
              setServerReachabilityStatus={this.props.setServerReachabilityStatus}
              timeOfCompletion={this.props.timeOfCompletion}
              setTreeFirstClicked={this.setTreeFirstClicked}
              hideSelectedEntry={hideSelectedEntry}
              editorLdapServer={this.props.editorLdapServer}
              wizardOperationInfo={wizardOperationInfo}
              refreshEntryTime={refreshEntryTime}
            />
        }

        { !showTreeView &&
          <EditorTableView
            key="ldap-editor-tableview"
            serverReachable={serverReachable}
            setServerReachabilityStatus={this.props.setServerReachabilityStatus}
            loading={loading}
            currentDn={currentDn}
            // Navigation items
            navItems={this.props.navItems}
            handleNavItemClick={this.props.handleNavItemClick}
            // Pagination
            itemCount={this.props.itemCount}
            page={this.props.page}
            perPage={this.props.perPage}
            onSetPage={this.props.onSetPage}
            onPerPageSelect={this.props.onPerPageSelect}
            variant={this.props.variant}
            // Table
            editorTableRows={this.props.editorTableRows}
            onCollapse={this.props.onCollapse}
            columns={this.props.columns}
            actionResolver={this.props.actionResolver}
            areActionsDisabled={this.props.areActionsDisabled}
          />
        }

      </React.Fragment>
    );
  }
}

export default EditorGenericView;
