import React from 'react';
import { Pagination } from '@patternfly/react-core';

import { MIN_PER_PAGE } from './constants.jsx';

class CompactPagination extends React.Component {
  constructor (props) {
    super(props);
    this.state = {
      page: 1,
      perPage: MIN_PER_PAGE
    };

    this.onSetPage = (_event, pageNumber) => {
      this.setState({
        page: pageNumber
      }, () => {
        this.props.updatePageSettings(this.props.paginationNode, pageNumber, this.state.perPage);
      });
    };

    this.onPerPageSelect = (_event, perPage) => {
      this.setState({
        perPage
      }, () => {
        // this.props.updatePagination(this.state.page, perPage);
        this.props.updatePageSettings(this.props.paginationNode, this.state.page, perPage);
      });
    };
  }

  componentDidMount () {
    // console.log(`CompactPagination - in componentDidMount() ==> ${this.state.perPage}`);
    // console.log(this.props.paginationNode);
    console.log(`this.props.childrenCurrentPage = ${this.props.childrenCurrentPage}`);
    console.log(`this.state.perPage = ${this.state.perPage}`);
    this.setState({
      page: this.props.childrenCurrentPage ? this.props.childrenCurrentPage : 1
    },
    () => {
      this.props.updatePageSettings(this.props.paginationNode, this.state.page, this.state.perPage);
    });
  }

  componentDidUpdate (prevProps) {
    if (this.props.paginationNode.goToFirstPage !== prevProps.paginationNode.goToFirstPage) {
      // this.setState({ page: this.props.childrenCurrentPage });
      console.log(`this.props.paginationNode.goToFirstPage = ${this.props.paginationNode.goToFirstPage}`);
    }
  }

  render () {
    return (
      <Pagination
        itemCount={this.props.itemCount}
        perPage={this.state.perPage}
        page={this.state.page}
        onSetPage={this.onSetPage}
        onPerPageSelect={this.onPerPageSelect}
        isCompact
      />
    );
  }
}

export default CompactPagination;
