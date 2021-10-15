import React from 'react';
import {
  Pagination,
  Dropdown, DropdownItem, DropdownPosition, DropdownToggle, DropdownToggleCheckbox,
  Toolbar, ToolbarGroup, ToolbarItem
} from '@patternfly/react-core';

import {
  Table, TableHeader, TableBody, TableVariant,
  SortByDirection
} from '@patternfly/react-table';

// TODO: Rename to GenericTable ( since it has pagination and also sorting and filtering capabilities )
class GenericPagination extends React.Component {
  constructor (props) {
    super(props);

    this.state = {
      itemCount: 0,
      page: 1,
      perPage: 10,
      pagedRows: [],
      // selectedRows: [],
      // sortedRows: this.props.enableSorting ? [...this.props.rows] : null,
      sortBy: {},
      isDropDownOpen: false,
      rows: [...this.props.rows]

    };

    this.onSort = (_event, index, direction) => {
      const mySortedRows = this.state.rows
        .sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
      this.setState({
        sortBy: {
          index,
          direction
        },
        rows: direction === SortByDirection.asc
          ? mySortedRows
          : mySortedRows.reverse()
      }, () => this.setState({ // Need to update this.state.rows prior to run getRowsToShow().
        pagedRows: this.getRowsToShow(this.state.page, this.state.perPage)
      }));
    }
  }

  componentDidMount () {
    this.setState({
      rows: [...this.props.rows]
    }, () => this.setState({
      // pagedRows: this.getRowsToShow(this.state.page, this.state.perPage),
      pagedRows: this.getRowsToShow(1, this.state.perPage),
      page: 1,
      itemCount: this.props.rows.length
    }));
  }

  componentDidUpdate (prevProps) {
    /* console.log('GenericPagination -  componentDidUpdate()');
    console.log(`prevProps.tableModificationTime = ${prevProps.tableModificationTime}`);
    console.log(`this.props.tableModificationTime = ${this.props.tableModificationTime}`);
    console.log(`GenericPagination -  componentDidUpdate() - this.props.rows.length=${this.props.rows.length}`);
    console.log(`GenericPagination -  componentDidUpdate() - this.state.pagedRows.length=${this.state.pagedRows.length}`); */

    if (this.props.tableModificationTime !== prevProps.tableModificationTime) {
      this.setState({
        rows: [...this.props.rows]
      }, () => this.setState({
        // pagedRows: this.getRowsToShow(this.state.page, this.state.perPage),
        pagedRows: this.getRowsToShow(1, this.state.perPage),
        page: 1,
        itemCount: this.props.rows.length
      }));
    }
  }

  onSelectRow = (_event, isSelected, rowId) => {
    // const rows = [...this.state.pagedRows];
    // rows[rowId].selected = isSelected;

    // Find the row in the full array and set 'selected' property accordingly
    // The rowId cannot be used since it changes with the pagination.
    // TODO: Need to make sure the column used has unique values for each row.
    // TODO: Pass the colum to use as a property.
    const cellValue = this.state.pagedRows[rowId].cells[0];
    const allItems = [...this.state.rows];
    const index = allItems.findIndex(item => item.cells[0] === cellValue);
    allItems[index].selected = isSelected;
    this.setState({
      rows: allItems,
      pagedRows: this.getRowsToShow(this.state.page, this.state.perPage)
    });
  };

  handleSelectClick = newState => {
    /* this.setState({
      allObjectClassesSelected: newState === 'all'
    }); */

    if (newState === 'none') {
      const newRows = this.state.rows.map(item => {
        item.selected = false;
        return item;
      });
      this.setState(
        {
          rows: newRows
        });
    } else if (newState === 'page') {
      // let newRows = [];
      const rows = this.state.pagedRows.map(item => {
        // const isSelected = item.selected;
        // newRows = isSelected ? [...newRows] : [...newRows, item];
        item.selected = true;
        return item;
      });

      // TODO: If selectedRows is not needed, use the simpler for of setState.
      this.setState((prevState, props) => {
        return {
          // selectedRows: prevState.selectedRows.concat(newRows),
          pagedRows: rows
        };
      });
    } else {
      const newRows = this.state.rows.map(item => {
        item.selected = true;
        return item;
      });
      this.setState(
        {
          rows: newRows
        });
    }
  };

  onDropDownToggle = isOpen => {
    this.setState({
      isDropDownOpen: isOpen
    });
  };

  onDropDownSelect = event => {
    this.setState((prevState, props) => {
      return { isDropDownOpen: !prevState.isDropDownOpen };
    });
  };

  onSetPage = (_event, pageNumber) => {
    this.setState({
      page: pageNumber,
      pagedRows: this.getRowsToShow(pageNumber, this.state.perPage)
    });
  };

  onPerPageSelect = (_event, perPage) => {
    this.setState({
      page: 1,
      perPage: perPage,
      pagedRows: this.getRowsToShow(1, perPage)
    });
  };

  getRowsToShow = (page, perPage) => {
    const start = (page - 1) * perPage;
    const end = page * perPage;
    /* if (this.props.enableSorting) {
      return this.state.sortedRows.slice(start, end);
    } */
    return this.state.rows.slice(start, end);
  }

  buildSelectDropdown () {
    const { isDropDownOpen, selectedRows, allObjectClassesSelected } = this.state;
    const numSelected = // allObjectClassesSelected === true
      // ? this.state.rows.length
      // : this.state.rows.filter(item => item.selected).length;
      this.state.rows.filter(item => item.selected).length;
      // selectedRows.length;
    const allSelected = numSelected === this.state.rows.length;
    const anySelected = numSelected > 0;
    const someChecked = anySelected ? null : false;
    // const isChecked = allSelected ? true : someChecked;

    const items = [
      <DropdownItem key="dd-item-1" onClick={() => this.handleSelectClick('none')}>
        Select none (0 item)
      </DropdownItem>,
      <DropdownItem key="dd-item-2" onClick={() => this.handleSelectClick('page')}>
        Select page ({this.state.perPage} items)
      </DropdownItem>,
      <DropdownItem key="dd-item-3" onClick={() => this.handleSelectClick('all')}>
        Select all ({this.state.rows.length} items)
      </DropdownItem>
    ];

    return (
      <Dropdown
        onSelect={this.onDropDownSelect}
        position={DropdownPosition.left}
        toggle={
          <DropdownToggle
            splitButtonItems={[
              <DropdownToggleCheckbox
                id="dropdown-checkbox"
                key="split-checkbox"
                aria-label={anySelected ? 'Deselect all' : 'Select all'}
                // https://github.com/facebook/react/issues/6779#issuecomment-572608962
                // gives some hints about the WARNING message below:
                // "A component is changing a controlled input of type checkbox to be uncontrolled."
                // isChecked={isChecked === true}.
                isChecked={allSelected ? true : someChecked}
                onClick={() => {
                  anySelected ? this.handleSelectClick('none') : this.handleSelectClick('all');
                }}
              />
            ]}
            onToggle={this.onDropDownToggle}
          >
            {numSelected !== 0 && <React.Fragment>{numSelected} selected</React.Fragment>}
          </DropdownToggle>
        }
        isOpen={isDropDownOpen}
        dropdownItems={items}
      />
    );
  }

  /* renderToolbar () {
    return (
      <Toolbar className="pf-l-toolbar pf-u-justify-content-space-between pf-u-mx-xl pf-u-my-md">
        <ToolbarGroup>
          <ToolbarItem className="pf-u-mr-md">{this.buildSelectDropdown()}</ToolbarItem>
        </ToolbarGroup>
        {this.renderPagination('top')}
      </Toolbar>
    );
  } */

  render () {
    const {
      itemCount, page, perPage, pagedRows,
      isDropDownOpen
    } = this.state;

    // Enable pagination if the number of rows is higher than 10.
    const showPagination = itemCount > 10;

    return (

      <React.Fragment>
        { showPagination &&
        <Toolbar className="pf-l-toolbar pf-u-justify-content-space-between pf-u-mx-xl pf-u-my-md">
          {this.props.isSelectable &&
          <ToolbarGroup>
            <ToolbarItem className="pf-u-mr-md">{this.buildSelectDropdown()}</ToolbarItem>
          </ToolbarGroup>
          }
          <Pagination
            itemCount={itemCount}
            page={page}
            perPage={perPage}
            onSetPage={this.onSetPage}
            onPerPageSelect={this.onPerPageSelect}
            // isCompact
            widgetId="pagination-options-menu-generic"
          />
        </Toolbar>
        }

        <Table
          variant={TableVariant.compact}
          rows={pagedRows}
          cells={this.props.columns}
          actions={this.props.actions}
          aria-label={this.props.ariaLabel}
          caption={this.props.caption}
          sortBy={this.props.enableSorting ? this.state.sortBy : null}
          onSort={this.props.enableSorting ? this.onSort : null}
          // sortBy={this.props.sortBy}
          // onSort={this.props.onSort}
          // onSelect={this.props.onSelect}
          canSelectAll={this.props.canSelectAll}
          onSelect={this.props.isSelectable ? this.onSelectRow : null}>
          <TableHeader />
          <TableBody />
        </Table>
      </React.Fragment>
    );
  }
}

export default GenericPagination;
