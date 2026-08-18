import cockpit from "cockpit";
import React from 'react';
import {
    Pagination,
    Spinner,
    Text,
    TextContent,
    TextVariants,
} from '@patternfly/react-core';
import {
    Table,
    Thead,
    Tr,
    Th,
    Tbody,
    Td,
    ExpandableRowContent,
    ActionsColumn
} from '@patternfly/react-table';

const _ = cockpit.gettext;

const EditorTableView = (props) => {
    const {
        loading,
        columns,
        editorTableRows,
        onCollapse,
        itemCount,
        page,
        perPage,
        onSetPage,
        onPerPageSelect,
        actionResolver // Added actionResolver from props
    } = props;

    let tableColumns = [...columns];
    let rows = [...editorTableRows];
    let noBackend = false;

    if (rows.length === 0) {
        tableColumns = ['Database Suffixes'];
        rows = [{ cells: ['No Databases'] }];
        noBackend = true;
    }

    // Helper function to safely render cell content
    const renderCellContent = (cell) => {
        if (cell === null || cell === undefined) {
            return '';
        }
        if (React.isValidElement(cell)) {
            return cell;
        }
        if (typeof cell === 'object') {
            if (cell.title) {
                return React.isValidElement(cell.title) 
                    ? cell.title 
                    : <span>{String(cell.title)}</span>;
            }
            return '';
        }
        return String(cell);
    };

    // Filter out the expanded content rows and create a map for them
    const [parentRows, expandedContentMap] = React.useMemo(() => {
        const parentRowsArray = [];
        const expandedMap = new Map();
        
        for (let i = 0; i < rows.length; i++) {
            const currentRow = rows[i];
            if (i % 2 === 0) { // Parent rows are at even indices
                parentRowsArray.push(currentRow);
                if (rows[i + 1]) { // Store expanded content if it exists
                    expandedMap.set(i, rows[i + 1]);
                }
            }
        }
        
        return [parentRowsArray, expandedMap];
    }, [rows]);

    const loadingBody = (
        <div className="ds-margin-top-xlg ds-center">
            <TextContent>
                <Text component={TextVariants.h3}>
                    {_("Loading ...")}
                </Text>
            </TextContent>
            <Spinner className="ds-margin-top-lg" size="lg" />
        </div>
    );

    const tableBody = (
        <div>
            <Table 
                aria-label="editor table view"
                variant='compact'
            >
                <Thead>
                    <Tr>
                        {!noBackend && (
                            <Th screenReaderText="Expand/Collapse Row" />
                        )}
                        {tableColumns.map((column, columnIndex) => (
                            <Th key={columnIndex}>
                                {typeof column === 'object' ? column.title : column}
                            </Th>
                        ))}
                        {!noBackend && actionResolver && <Th screenReaderText="Actions" />}
                    </Tr>
                </Thead>
                <Tbody>
                    {parentRows.map((row, rowIndex) => (
                        <React.Fragment key={rowIndex}>
                            <Tr>
                                {!noBackend && (
                                    <Td
                                        expand={{
                                            rowIndex,
                                            isExpanded: row.isOpen,
                                            onToggle: () => onCollapse(null, rowIndex * 2, !row.isOpen, row)
                                        }}
                                    />
                                )}
                                {row.cells.map((cell, cellIndex) => (
                                    <Td 
                                        key={cellIndex} 
                                        dataLabel={tableColumns[cellIndex]?.title || tableColumns[cellIndex]}
                                    >
                                        {renderCellContent(cell)}
                                    </Td>
                                ))}
                                {!noBackend && actionResolver && (
                                    <Td isActionCell>
                                        <ActionsColumn 
                                            items={actionResolver(row, { rowIndex: rowIndex * 2 })}
                                        />
                                    </Td>
                                )}
                            </Tr>
                            {row.isOpen && expandedContentMap.has(rowIndex * 2) && (
                                <Tr isExpanded={true}>
                                    <Td />
                                    <Td 
                                        colSpan={tableColumns.length + (noBackend ? 0 : (actionResolver ? 2 : 1))}
                                        noPadding
                                    >
                                        <ExpandableRowContent>
                                            {renderCellContent(expandedContentMap.get(rowIndex * 2).cells[0])}
                                        </ExpandableRowContent>
                                    </Td>
                                </Tr>
                            )}
                        </React.Fragment>
                    ))}
                </Tbody>
            </Table>
            {!noBackend && (
                <Pagination
                    id="ds-addons-editor-view-top"
                    className="ds-margin-top"
                    widgetId="pagination-options-menu-top"
                    itemCount={itemCount}
                    page={page}
                    perPage={perPage}
                    onSetPage={(_evt, value) => onSetPage(value)}
                    onPerPageSelect={(_evt, value) => onPerPageSelect(value)}
                    isCompact
                    dropDirection="up"
                />
            )}
        </div>
    );

    return (
        <div className="ds-indent-lg ds-margin-top-lg">
            {loading ? loadingBody : tableBody}
        </div>
    );
};

export default EditorTableView;
