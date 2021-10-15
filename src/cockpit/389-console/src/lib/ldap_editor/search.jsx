import React from "react";
import {
    Checkbox,
    Grid,
    GridItem,
    ExpandableSection,
    Form,
    FormHelperText,
    FormSelect,
    FormSelectOption,
    Label,
    NumberInput,
    SearchInput,
    Select,
    SelectOption,
    SelectVariant,
    Spinner,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    ToggleGroup,
    ToggleGroupItem,
    ValidatedOptions
} from "@patternfly/react-core";
import {
    Table,
    TableHeader,
    TableBody,
    TableVariant,
    EditableTextCell,
    expandable
} from '@patternfly/react-table';
import {
    AngleRightIcon,
} from '@patternfly/react-icons';
import PropTypes from "prop-types";
import {
    getSearchEntries, getBaseLevelEntryAttributes,
} from './lib/utils.jsx';
import { ENTRY_MENU } from './lib/constants.jsx';
import EditorTableView from './tableView.jsx';
import { valid_dn } from '../tools.jsx';
import GenericWizard from './wizards/genericWizard.jsx';

export class SearchDatabase extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            searching: false,
            loading: false,
            searchBase: "",
            searchFilter: "",
            searchScope: "sub",
            sizeLimit: 1000,
            timeLimit: 30,
            isExpanded: false,
            searchSuffix: "",
            searchType: 'Search Text',
            searchText: "",
            getOperationalAttrs: false,
            total: 0,
            // Search attributes
            cn: true,
            uid: true,
            sn: true,
            givenName: true,
            mail: true,
            displayName: false,
            legalName: false,
            memberOf: false,
            member: false,
            uniqueMember: false,
            customSearchAttrs: [],
            isCustomAttrOpen: false,
            // Table
            columns: [
                {
                    title: 'Entry DN',
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
            page: 0,
            perPage: 10,
            pagedRows: [],
            loading: false,
            wizardName: '',
            isWizardOpen: false,
            wizardEntryDn: '',
            treeViewRootSuffixes: [], // TODO when aci's are ready (is there a better list of suffixes?)
        }

        this.initialResultText = 'Loading...';

        this.onToggle = isExpanded => {
            this.setState({
                isExpanded
            });
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
            this.setState({ wizardOperationInfo });
        };

        // Custom filter attributes
        this.onCustomAttrToggle = isCustomAttrOpen => {
            this.setState({
                isCustomAttrOpen,
            });
        };
        this.onCustomAttrClear = () => {
            this.setState({
                customSearchAttrs: [],
                isCustomAttrOpen: false
            });
        };
        this.onCustomAttrChange = (selections) => {
            this.setState({
                customSearchAttrs: selections,
                isCustomAttrOpen: false,
            }, () => { this.buildSearchFilter(this.state.searchText) });
        };

        this.buildSearchFilter = (value) => {
            let searchFilter = "";
            if (this.state.searchType === "Search Text") {
                // Build filter from attributes
                //
                // (|(attr1=*VALUE*)(attr2=*VALUE*)...)
                let attrs = [];
                const chkBoxAttrs = [
                    'cn', 'uid', 'sn', 'givenName', 'mail', 'displayName',
                    'legalName', 'memberOf', 'member', 'uniqueMember'
                ];
                for (const attr of chkBoxAttrs) {
                    if (this.state[attr]) {
                        attrs.push(attr);
                    }
                }
                attrs = attrs.concat(this.state.customSearchAttrs);
                if (attrs.length == 0) {
                    return;
                }
                if (attrs.length > 1) {
                    searchFilter = "(|";
                    for (const attr of attrs) {
                        searchFilter += "(" + attr + "=*" + value + "*)"
                    }
                    searchFilter += ")";
                } else {
                    searchFilter = "(" + attrs[0] + "=*" + value + "*)"
                }
            } else {
                // Value is the LDAP search filter
                searchFilter = value;
            }
            return searchFilter;
        };

        this.onSearchChange = (value, event) => {
            this.setState({
                searchText: value,
                searchFilter: this.buildSearchFilter(value),
            });
        };

        this.onSearch = () => {
            if (!this.state.searchFilter) {
                return;
            }
            // Do search
            this.setState({
                isExpanded: false,
                searching: true,
            });

            let params = {
                serverId: this.props.serverId,
                searchBase: this.state.searchBase,
                searchFilter: this.state.searchFilter,
                searchScope: this.state.searchScope,
                sizeLimit: this.state.sizeLimit,
                timeLimit: this.state.timeLimit,
            };
            getSearchEntries(params, this.processResults);
        };

        this.handleSearchTypeClick = (isSelected, event) => {
            const id = event.currentTarget.id;
            this.setState({
                searchType: id,
                searchText: "",
                searchFilter: "",
            });
        };

        this.handleScopeClick = (isSelected, event) => {
            const id = event.currentTarget.id;
            this.setState({ searchScope: id });
        };

        this.maxValue = 20000000;
        this.onMinus = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            });
        };
        this.onNumberChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            });
        };
        this.onPlus = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            });
        };

        this.clearSearchBase = () => {
            this.setState({
                searchBase: this.state.searchSuffix
            });
        }

        this.handleChange = this.handleChange.bind(this);
        this.handleSearchChange = this.handleSearchChange.bind(this);
        this.handleSuffixChange = this.handleSuffixChange.bind(this);
        this.handleCustomAttrChange = this.handleCustomAttrChange.bind(this);
        this.getPageData = this.getPageData.bind(this);
        this.processResults = this.processResults.bind(this);
        this.handleCollapse = this.handleCollapse.bind(this);
        this.actionResolver = this.actionResolver.bind(this);
    }

    componentDidMount() {
        const suffixList =  this.props.suffixList;
        const searchBase = this.props.searchBase;
        this.setState({
            searchBase: searchBase ? searchBase : suffixList.length > 0 ? suffixList[0] : "",
            searchSuffix: this.props.suffixList.length > 0 ? this.props.suffixList[0] : "",
        });
    }

    handleCollapse (event, rowKey, isOpen, data) {
        const { pagedRows } = this.state;
        pagedRows[rowKey].isOpen = isOpen;
        this.setState({
            pagedRows
        });

        const firstTime = (pagedRows[rowKey + 1].cells[0].title) === this.initialResultText;
        if (firstTime) {
            const baseDn = pagedRows[rowKey].rawdn; // The DN is the first element in the array.
            getBaseLevelEntryAttributes(this.props.serverId, baseDn, (entryArray) => {
                pagedRows[rowKey + 1].cells = [{
                    title: (
                        <>
                            {entryArray.map((line) => (
                                <div key={line.attribute + line.value}>
                                    <strong>{line.attribute}</strong>
                                    {line.value.toLowerCase() === ": ldapsubentry" ? <span className="ds-info-color">{line.value}</span> : line.value}
                                </div>
                            ))}
                        </>
                    )
                }];
                // Update the row.
                this.setState({
                    pagedRows
                });
            });
        }
    }

    // Process the entries that are direct children.
    processResults = (searchResults) => {
        const resultRows = [];
        let rowNumber = 0;

        if (searchResults) {
            searchResults.map(aChild => {
                const info = JSON.parse(aChild);

                // TODO Test for a JPEG photo!!!

                let dn = info.dn;
                if (info.ldapsubentry) {
                    dn =
                        <div className="ds-info-icon">
                            {info.dn} <InfoCircleIcon title="This is a hidden LDAP subentry" className="ds-info-icon" />
                        </div>;
                }

                resultRows.push(
                    {
                        isOpen: false,
                        cells: [
                            { title: dn },
                            info.numSubordinates,
                            info.modifyTimestamp,
                        ],
                        rawdn: info.dn
                    },
                    {
                        parent: rowNumber,
                        cells: [
                            { title: this.initialResultText }
                        ]
                    });

                // Increment by 2 the row number.
                rowNumber += 2;
            });
        }

        this.setState({
            searching: false,
            rows: resultRows,
            // Each row is composed of a parent and its single child.
            pagedRows: resultRows.slice(0, 2 * this.state.perPage),
            total: resultRows.length / 2,
            page: 1
        });
    }

    handleCustomAttrChange (value) {
        this.setState({
            customSearchAttrs: value
        });
    }

    handleSearchChange (e) {
        const value = e.target.value;
        this.setState({
            searchType: value
        });
    }

    handleSuffixChange (e) {
        const value = e.target.value;
        this.setState({
            searchSuffix: value,
            searchBase: value,
        });
    }

    handleChange (e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value.trim();
        this.setState({
            [e.target.id]: value,
        }, () => {
            this.setState({
                searchFilter: this.buildSearchFilter(this.state.searchText),
            });
        });
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
            let i = 0;
            for (i; i < pagedRows.length - 1; i++) {
                if (i % 2 === 0) {
                    pagedRows[i + 1].parent = i;
                }
            }
            this.setState({ pagedRows, perPage, page, loading: false });
        }
    }

    actionResolver = (rowData, { rowIndex }) => {
        // No action on the children.
        if ((rowIndex % 2) === 1) {
            return null;
        }

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
                title: 'Edit ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.edit,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true
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
                        isWizardOpen: true
                    });
                }
            },
            {
                isSeparator: true
            },
            {
                title: 'ACIs ...',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.acis,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true
                    });
                }
            },
            {
                title: 'Roles ...',
                isDisabled: true,
                onClick: (event, rowId, rowData, extra) => {
                    // TODO
                    console.log(`clicked on Third action, on row ${rowId}`);
                    console.log('extra = ' + extra);
                }
            },
            {
                title: 'Smart Referrals ...',
                isDisabled: true
            },
            {
                isSeparator: true
            },
            {
                title: 'Delete',
                onClick:
                () => {
                    this.setState({
                        wizardName: ENTRY_MENU.delete,
                        wizardEntryDn: rowData.rawdn,
                        isWizardOpen: true
                    });
                }
            }];

        return [
            ...updateActions,
        ];
    }

    render() {
        const {
            showModal,
            closeHandler,
            logData,
            suffixList
        } = this.props;

        const {
            baseDN,
            page,
            perPage,
            total,
            searching,
            wizardName,
            isWizardOpen,
            wizardEntryDn,
        } = this.state;

        let has_rows = true;
        let columns = this.state.columns;
        let pagedRows = this.state.pagedRows;

        if (pagedRows.length == 0) {
            has_rows = false;
            columns = [' '];
            pagedRows = [{ cells: ['No Search Results'] }];
        }

        const treeItemsProps = wizardName === 'acis'
            ? { treeViewRootSuffixes: this.state.treeViewRootSuffixes }
            : {}

        return (
            <div>
                {isWizardOpen && (
                    <GenericWizard
                        wizardName={wizardName}
                        isWizardOpen={isWizardOpen}
                        toggleOpenWizard={this.toggleOpenWizard}
                        wizardEntryDn={wizardEntryDn}
                        editorLdapServer={this.props.serverId}
                        {...treeItemsProps}
                        setWizardOperationInfo={this.setWizardOperationInfo}
                        onReload={this.onSearch}
                        allObjectclasses={this.props.allObjectclasses}
                    />
                )}
                <Form className="ds-margin-top-lg" isHorizontal autoComplete="off">
                    <Grid className="ds-margin-left">
                        <div className="ds-container">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Search Database
                                </Text>
                            </TextContent>
                            <Grid className="ds-left-margin">
                                <GridItem span={4}>
                                    <FormSelect
                                        id="searchSuffix"
                                        value={this.state.searchSuffix}
                                        onChange={(value, event) => {
                                            this.handleSuffixChange(event);
                                        }}
                                        aria-label="FormSelect Input"
                                        className="ds-instance-select ds-raise-field"
                                    >
                                        {suffixList.map((suffix, index) => (
                                            <FormSelectOption key={suffix} value={suffix} label={suffix} />
                                        ))}
                                    </FormSelect>
                                </GridItem>
                                <GridItem span={8}>
                                    { this.state.searchSuffix !== this.state.searchBase ? <Label onClose={this.clearSearchBase} className="ds-left-margin" color="blue">{this.state.searchBase}</Label> : "" }
                                </GridItem>
                            </Grid>
                        </div>
                    </Grid>
                    <Grid className="ds-margin-left">
                        <GridItem span={12}>
                            <div className="ds-container">
                                <ToggleGroup aria-label="Default with single selectable">
                                    <ToggleGroupItem
                                        title="Text that will be used with pre-selected attributes to find matching entries."
                                        text="Text"
                                        buttonId="Search Text"
                                        isSelected={this.state.searchType === "Search Text"}
                                        onChange={this.handleSearchTypeClick}
                                    />
                                    <ToggleGroupItem
                                        title="Specific LDAP search filter for finding entries."
                                        text="Filter"
                                        buttonId="Search Filter"
                                        isSelected={this.state.searchType === "Search Filter"}
                                        onChange={this.handleSearchTypeClick}
                                    />
                                </ToggleGroup>
                                <SearchInput
                                    placeholder={this.state.searchType == "Search Text" ? "Enter search text ..." : "Enter an LDAP search filter ..."}
                                    value={this.state.searchText}
                                    onChange={this.onSearchChange}
                                    onClear={evt => this.onSearchChange('', evt)}
                                    onSearch={this.onSearch}
                                    className="ds-search-input"
                                />
                            </div>
                        </GridItem>
                    </Grid>

                    <ExpandableSection
                        className="ds-margin-left"
                        toggleText={this.state.isExpanded ? 'Hide Search Criteria' : 'Show Search Criteria'}
                        onToggle={this.onToggle}
                        isExpanded={this.state.isExpanded}
                        displaySize={this.state.isExpanded ? "large" : "default"}
                    >
                        <Grid className="ds-margin-left">
                            <GridItem span={2} className="ds-label">
                                Search Base
                            </GridItem>
                            <GridItem span={6}>
                                <TextInput
                                    value={this.state.searchBase}
                                    type="text"
                                    id="searchBase"
                                    aria-describedby="searchBase"
                                    name="searchBase"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={!valid_dn(this.state.searchBase) ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                            <GridItem span={2} className="ds-left-margin ds-lower-field-md" >
                                <FormHelperText isError isHidden={valid_dn(this.state.searchBase)}>
                                    Invalid DN syntax
                                </FormHelperText>
                            </GridItem>

                        </Grid>
                        <Grid className="ds-margin-left ds-margin-top">
                            <GridItem span={2} className="ds-label">
                                Search Scope
                            </GridItem>
                            <GridItem span={4}>
                                <ToggleGroup aria-label="search scope">
                                    <ToggleGroupItem
                                        text="Subtree"
                                        buttonId="sub"
                                        isSelected={this.state.searchScope === "sub"}
                                        onChange={this.handleScopeClick}
                                        title="Search for entries starting at the search base, and including all its child entries"
                                    />
                                    <ToggleGroupItem
                                        text="One Level"
                                        buttonId="one"
                                        isSelected={this.state.searchScope === "one"}
                                        onChange={this.handleScopeClick}
                                        title="Search for entries starting at the search base, and include only the first level of child entries"
                                    />
                                    <ToggleGroupItem
                                        text="Base"
                                        buttonId="base"
                                        isSelected={this.state.searchScope === "base"}
                                        onChange={this.handleScopeClick}
                                        title="Search for an exact entry (search base). This does not include child entries."
                                    />
                                </ToggleGroup>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-left ds-margin-top">
                            <GridItem span={2} className="ds-label">
                                Size Limit
                            </GridItem>
                            <GridItem span={10}>
                                <NumberInput
                                    value={this.state.sizeLimit}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinus("sizeLimit") }}
                                    onChange={(e) => { this.onNumberChange(e, "sizeLimit", -1) }}
                                    onPlus={() => { this.onPlus("sizeLimit") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-left ds-margin-top" title="Search timeout in seconds">
                            <GridItem span={2} className="ds-label">
                                Time Limit
                            </GridItem>
                            <GridItem span={10}>
                                <NumberInput
                                    value={this.state.timeLimit}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinus("timeLimit") }}
                                    onChange={(e) => { this.onNumberChange(e, "timeLimit", -1) }}
                                    onPlus={() => { this.onPlus("timeLimit") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <div hidden={this.state.searchType == "Search Filter"}>
                            <Grid
                                className="ds-margin-left ds-margin-top"
                                title="Only used for Search Text based queries.  The selected attributes will use Search Text as the attribute value in the search filter"
                            >
                                <GridItem span={2} className="ds-label">
                                    Search Attributes
                                </GridItem>
                            </Grid>
                            <div className="ds-indent">
                                <Grid className="ds-margin-left ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="cn"
                                            id="cn"
                                            isChecked={this.state.cn}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="cn"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="uid"
                                            id="uid"
                                            isChecked={this.state.uid}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="uid"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="sn"
                                            id="sn"
                                            isChecked={this.state.sn}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="sn"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-left ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="givenName"
                                            id="givenName"
                                            isChecked={this.state.givenName}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="givenName"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="mail"
                                            id="mail"
                                            isChecked={this.state.mail}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="mail"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="displayName"
                                            id="displayName"
                                            isChecked={this.state.displayName}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="displayName"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-left ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="legalName"
                                            id="legalName"
                                            isChecked={this.state.legalName}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="legalName"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="memberOf"
                                            id="memberOf"
                                            isChecked={this.state.memberOf}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="memberOf"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="member"
                                            id="member"
                                            isChecked={this.state.member}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="member"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-left ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            label="uniqueMember"
                                            id="uniqueMember"
                                            isChecked={this.state.uniqueMember}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            aria-label="uniqueMember"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-left ds-margin-top" title="Enter space or comma separateed list of attributes to search.">
                                    <GridItem span={8}>
                                        <Select
                                            variant={SelectVariant.typeaheadMulti}
                                            typeAheadAriaLabel="Type attributes to include in filter ..."
                                            onToggle={this.onCustomAttrToggle}
                                            onSelect={(e, selection) => {
                                                this.onCustomAttrChange(selection);
                                            }}
                                            onClear={this.onCustomAttrClear}
                                            selections={this.state.customSearchAttrs}
                                            isOpen={this.state.isCustomAttrOpen}
                                            aria-labelledby="typeAhead-attr-filter"
                                            placeholderText="Type attributes to include in the filter ..."
                                            noResultsFoundText="There are no matching attributes"
                                        >
                                            {this.props.attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                            ))}
                                        </Select>
                                    </GridItem>
                                </Grid>
                            </div>
                        </div>
                    </ExpandableSection>
                </Form>
                <div className="ds-indent">
                    <div className={this.state.searching ? "ds-margin-top-xlg ds-center" : "ds-hidden"}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Searching <i>{this.state.searchBase}</i> ...
                            </Text>
                        </TextContent>
                        <Spinner className="ds-margin-top-lg" size="xl" />
                    </div>
                    <div className={searching ? "ds-hidden" : ""}>
                        <EditorTableView
                            key={searching}
                            loading={searching}
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
                            actionResolver={pagedRows.length < 2 ? null: this.actionResolver}
                        />
                    </div>
                </div>
            </div>
        );
    }
}

SearchDatabase.propTypes = {
    attributes: PropTypes.array,
    searchBase: PropTypes.string,
};

SearchDatabase.defaultProps = {
    attributes: [],
    searchBase: "",
};
